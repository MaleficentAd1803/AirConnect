#include "core/updates.h"
#include "core/preferences.h"
#include "version.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
// The protocol core uses -fno-char8_t for its original UTF-8 filesystem contracts.
#define JSON_HAS_CPP_17
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace ac {
namespace {
struct Internet {
    HINTERNET handle = nullptr;
    explicit Internet(HINTERNET value) : handle(value) {
        if (!handle)
            throw std::runtime_error("Unable to open a secure update connection.");
    }
    ~Internet() {
        if (handle)
            WinHttpCloseHandle(handle);
    }
    operator HINTERNET() const {
        return handle;
    }
    Internet(const Internet &) = delete;
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("SHA-256 is unavailable.");
        DWORD n = 0, length = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&length, sizeof(length), &n, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("SHA-256 initialization failed.");
        }
        object.resize(length);
        if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("SHA-256 initialization failed.");
        }
    }
    ~Hash() {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(const char *data, DWORD size) {
        if (BCryptHashData(hash, (PUCHAR)data, size, 0) < 0)
            throw std::runtime_error("SHA-256 calculation failed.");
    }
    std::string finish() {
        std::array<unsigned char, 32> result{};
        if (BCryptFinishHash(hash, result.data(), result.size(), 0) < 0)
            throw std::runtime_error("SHA-256 verification failed.");
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (auto c : result)
            out << std::setw(2) << unsigned(c);
        return "sha256:" + out.str();
    }
};
template <class Sink>
void request(const std::string &url, uint64_t limit, const std::atomic<bool> &cancel, Sink sink) {
    auto address = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("The update address must use HTTPS.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (host != L"api.github.com" && host != L"github.com")
        throw std::runtime_error("Unexpected update server.");
    Internet session(WinHttpOpen(L"AirConnect/" AC_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
    Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    Internet query(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(query, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    const wchar_t *headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (cancel || !WinHttpSendRequest(query, headers, DWORD(-1), nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(query, nullptr))
        throw std::runtime_error(
            "GitHub could not be reached. Check your Internet connection and try again.");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(query, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Invalid update response.");
    if (status == 404)
        throw std::runtime_error("No published release is available yet.");
    if (status == 403 || status == 429)
        throw std::runtime_error("GitHub's request limit was reached. Try again later.");
    if (status != 200)
        throw std::runtime_error("GitHub returned an unsuccessful update response.");
    std::array<char, 65536> buffer{};
    uint64_t total = 0;
    DWORD count = 0;
    while (!cancel) {
        if (!WinHttpReadData(query, buffer.data(), buffer.size(), &count))
            throw std::runtime_error("The update download was interrupted.");
        if (!count)
            return;
        total += count;
        if (total > limit)
            throw std::runtime_error("The update exceeded its allowed size.");
        sink(buffer.data(), count);
    }
    throw std::runtime_error("Update cancelled.");
}
std::array<int, 3> version(const std::string &text) {
    std::array<int, 3> result{};
    size_t pos = 0;
    if (!text.empty() && text[0] == 'v')
        ++pos;
    for (int part = 0; part < 3; ++part) {
        size_t begin = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            ++pos;
        if (pos == begin || pos - begin > 5)
            throw std::runtime_error("Invalid release version.");
        result[part] = std::stoi(text.substr(begin, pos - begin));
        if (part != 2 && (pos == text.size() || text[pos++] != '.'))
            throw std::runtime_error("Invalid release version.");
    }
    if (pos != text.size())
        throw std::runtime_error("Invalid release version.");
    return result;
}
} // namespace
bool newer_version(const std::string &candidate, const std::string &current) {
    return version(candidate) > version(current);
}
ReleaseUpdate parse_release(const std::string &text) {
    if (text.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Release metadata is too large.");
    auto j = nlohmann::json::parse(text);
    if (j.value("draft", true) || j.value("prerelease", true))
        throw std::runtime_error("This is not a stable release.");
    ReleaseUpdate release;
    auto tag = j.at("tag_name").get<std::string>();
    version(tag);
    if (tag.empty() || tag[0] != 'v')
        throw std::runtime_error("Unexpected release tag.");
    release.version = tag.substr(1);
    std::string name = "AirConnect-" + release.version + "-Setup.exe",
                expected =
                    "https://github.com/" AC_GITHUB_REPOSITORY "/releases/download/" + tag + "/" + name;
    const auto &assets = j.at("assets");
    if (!assets.is_array() || assets.size() > 100)
        throw std::runtime_error("Invalid release assets.");
    for (const auto &a : assets)
        if (a.value("name", std::string{}) == name) {
            if (!release.url.empty())
                throw std::runtime_error("Duplicate installer asset.");
            release.url = a.at("browser_download_url").get<std::string>();
            release.digest = a.at("digest").get<std::string>();
            release.bytes = a.at("size").get<uint64_t>();
            if (release.url != expected || release.bytes < 1024 || release.bytes > 150 * 1024 * 1024 ||
                release.digest.size() != 71 || release.digest.substr(0, 7) != "sha256:" ||
                release.digest.find_first_not_of("0123456789abcdef", 7) != std::string::npos)
                throw std::runtime_error("The installer metadata could not be verified.");
        }
    if (release.url.empty())
        throw std::runtime_error("This release does not include a Windows installer.");
    return release;
}
ReleaseUpdate latest_release(const std::atomic<bool> &stop) {
    std::string text;
    request("https://api.github.com/repos/" AC_GITHUB_REPOSITORY "/releases/latest", 2 * 1024 * 1024, stop,
            [&](const char *p, DWORD n) { text.append(p, n); });
    return parse_release(text);
}
ReleaseUpdate latest_release() {
    std::atomic<bool> stop{false};
    return latest_release(stop);
}
void download_release(const ReleaseUpdate &release, const std::filesystem::path &destination,
                      const std::atomic<bool> &cancel) {
    std::filesystem::create_directories(destination.parent_path());
    auto partial = destination;
    partial += L".part";
    std::error_code ignored;
    try {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("The update folder could not be written.");
        Hash hash;
        uint64_t bytes = 0;
        request(release.url, release.bytes, cancel, [&](const char *p, DWORD n) {
            file.write(p, n);
            if (!file)
                throw std::runtime_error("The update could not be saved. Check free disk space.");
            hash.add(p, n);
            bytes += n;
        });
        file.close();
        if (bytes != release.bytes || hash.finish() != release.digest)
            throw std::runtime_error(
                "The downloaded installer failed its SHA-256 check. It will not be opened.");
        if (!MoveFileExW(partial.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("The verified installer could not be saved.");
    } catch (...) {
        std::filesystem::remove(partial, ignored);
        throw;
    }
}
UpdateService::~UpdateService() {
    cancel = true;
    if (worker.joinable())
        worker.join();
}
UpdateStatus UpdateService::status() const {
    std::lock_guard lock(mutex);
    return current;
}
void UpdateService::set(UpdateStatus value) {
    std::lock_guard lock(mutex);
    current = std::move(value);
}
void UpdateService::check() {
    auto s = status();
    if (s.state == UpdateState::Checking || s.state == UpdateState::Downloading)
        return;
    if (worker.joinable())
        worker.join();
    s = {};
    s.state = UpdateState::Checking;
    s.message = L"Checking GitHub releases…";
    set(s);
    worker = std::thread([this] {
        UpdateStatus s;
        try {
            s.release = latest_release(cancel);
            bool available = newer_version(s.release.version, AC_VERSION);
            s.state = available ? UpdateState::Available : UpdateState::Current;
            s.message = available ? L"Version " + wide(s.release.version) + L" is available."
                                  : L"You have the latest version (" AC_VERSION_W L").";
        } catch (const std::exception &e) {
            s.state = UpdateState::Error;
            s.message = wide(e.what());
        }
        set(std::move(s));
    });
}
void UpdateService::download() {
    auto s = status();
    if (s.state != UpdateState::Available)
        return;
    if (worker.joinable())
        worker.join();
    s.state = UpdateState::Downloading;
    s.message = L"Downloading and verifying the installer…";
    set(s);
    worker = std::thread([this, s]() mutable {
        try {
            s.installer =
                data_directory() / L"Updates" / (L"AirConnect-" + wide(s.release.version) + L"-Setup.exe");
            download_release(s.release, s.installer, cancel);
            s.state = UpdateState::Ready;
            s.message = L"Version " + wide(s.release.version) + L" is ready to install.";
        } catch (const std::exception &e) {
            s.state = UpdateState::Error;
            s.message = wide(e.what());
        }
        set(std::move(s));
    });
}
} // namespace ac
