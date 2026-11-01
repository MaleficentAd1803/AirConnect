#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include "version.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <fdi.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
HWND window{}, path_edit{}, desktop{}, install_button{}, status_label{}, progress{};
HFONT font{}, heading{};
std::thread worker;
std::atomic<bool> busy{false};
std::wstring failure;
std::filesystem::path extraction_dir;
std::wstring update_directory;
DWORD wait_pid = 0;
bool update_mode = false;
std::vector<std::wstring> installed;
constexpr wchar_t uninstall_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AirConnect";
std::wstring module_path() {
    std::wstring s(32768, 0);
    s.resize(GetModuleFileNameW(nullptr, s.data(), (DWORD)s.size()));
    return s;
}
std::wstring known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        s = p;
        CoTaskMemFree(p);
    }
    return s;
}
std::wstring wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n < 1)
        return {};
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    out.resize(n - 1);
    return out;
}
bool flat_name(const std::wstring &s) {
    return !s.empty() && s.size() < 180 && s != L"." && s != L".." &&
           s.find_first_of(L"/\\:") == std::wstring::npos;
}
bool shortcut(const fs::path &name, const fs::path &target) {
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(link.GetAddressOf()))))
        return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(target.parent_path().c_str());
    link->SetDescription(L"AirConnect native AirPlay receiver");
    return SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(name.c_str(), TRUE));
}
bool registration(const fs::path &destination, bool desktop_shortcut) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, uninstall_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return false;
    auto put = [&](const wchar_t *name, const std::wstring &value) {
        return RegSetValueExW(key, name, 0, REG_SZ, (BYTE *)value.c_str(), (DWORD)((value.size() + 1) * 2)) ==
               ERROR_SUCCESS;
    };
    bool ok = put(L"DisplayName", L"AirConnect") && put(L"DisplayVersion", AC_VERSION_W) &&
              put(L"Publisher", L"AirConnect contributors") &&
              put(L"InstallLocation", destination.wstring()) &&
              put(L"DisplayIcon", (destination / L"AirConnect.exe").wstring()) &&
              put(L"UninstallString", L"\"" + (destination / L"Uninstall.exe").wstring() + L"\"");
    DWORD one = 1;
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (BYTE *)&one, 4);
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (BYTE *)&one, 4);
    RegCloseKey(key);
    auto menu = fs::path(known(FOLDERID_Programs)) / L"AirConnect";
    std::error_code ec;
    fs::create_directories(menu, ec);
    ok = ok && !ec && shortcut(menu / L"AirConnect.lnk", destination / L"AirConnect.exe") &&
         shortcut(menu / L"Uninstall AirConnect.lnk", destination / L"Uninstall.exe");
    if (desktop_shortcut)
        ok = ok &&
             shortcut(fs::path(known(FOLDERID_Desktop)) / L"AirConnect.lnk", destination / L"AirConnect.exe");
    return ok;
}
void *DIAMONDAPI cab_alloc(ULONG size) {
    return std::malloc(size);
}
void DIAMONDAPI cab_free(void *p) {
    std::free(p);
}
INT_PTR DIAMONDAPI cab_open(char *name, int flags, int mode) {
    return _open(name, flags | _O_BINARY, mode);
}
UINT DIAMONDAPI cab_read(INT_PTR f, void *p, UINT n) {
    return _read((int)f, p, n);
}
UINT DIAMONDAPI cab_write(INT_PTR f, void *p, UINT n) {
    return _write((int)f, p, n);
}
int DIAMONDAPI cab_close(INT_PTR f) {
    return _close((int)f);
}
long DIAMONDAPI cab_seek(INT_PTR f, long pos, int origin) {
    return _lseek((int)f, pos, origin);
}
INT_PTR DIAMONDAPI cab_notify(FDINOTIFICATIONTYPE type, PFDINOTIFICATION info) {
    if (type == fdintCOPY_FILE) {
        auto name = wide(info->psz1);
        if (!flat_name(name) || info->cb < 0 || info->cb > 128 * 1024 * 1024 || installed.size() > 100)
            return -1;
        auto path = extraction_dir / name;
        int fd = _wopen(path.c_str(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd < 0)
            return -1;
        installed.push_back(name);
        if (window)
            PostMessageW(window, WM_APP + 2, (WPARAM)installed.size(), 0);
        return fd;
    }
    if (type == fdintCLOSE_FILE_INFO) {
        _close((int)info->hf);
        return TRUE;
    }
    return 0;
}
bool extract(const fs::path &destination) {
#ifdef AC_UNINSTALLER
    (void)destination;
    return false;
#else
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
    if (!resource)
        return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    DWORD size = SizeofResource(nullptr, resource);
    void *bytes = LockResource(loaded);
    if (!bytes || size > 256 * 1024 * 1024)
        return false;
    wchar_t temp[MAX_PATH], cab[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"acb", 0, cab))
        return false;
    HANDLE output =
        CreateFileW(cab, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    DWORD written = 0;
    bool ok = output != INVALID_HANDLE_VALUE && WriteFile(output, bytes, size, &written, nullptr) &&
              written == size;
    if (output != INVALID_HANDLE_VALUE)
        CloseHandle(output);
    if (!ok) {
        DeleteFileW(cab);
        return false;
    }
    // FDI's input path API is ANSI; use an ASCII temporary path via the Windows short-name API.
    wchar_t short_name[MAX_PATH];
    if (!GetShortPathNameW(cab, short_name, MAX_PATH)) {
        DeleteFileW(cab);
        return false;
    }
    char ansi[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, short_name, -1, ansi, MAX_PATH, nullptr,
                             nullptr)) {
        DeleteFileW(cab);
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        DeleteFileW(cab);
        return false;
    }
    extraction_dir = fs::absolute(destination);
    installed.clear();
    ERF error{};
    HFDI fdi = FDICreate(cab_alloc, cab_free, cab_open, cab_read, cab_write, cab_close, cab_seek, cpuUNKNOWN,
                         &error);
    if (fdi) {
        ok = FDICopy(fdi, ansi, (char *)"", 0, cab_notify, nullptr, nullptr);
        FDIDestroy(fdi);
    } else
        ok = false;
    DeleteFileW(cab);
    if (ok) {
        std::wofstream manifest(destination / L"install-files.txt", std::ios::trunc);
        for (auto &file : installed)
            manifest << file << L'\n';
        manifest << L"install-files.txt\n";
        ok = manifest.good();
    }
    return ok;
#endif
}
bool upgrade(const fs::path &destination, DWORD pid, bool test_only = false) {
    if (!fs::exists(destination / L"AirConnect.exe") || !fs::exists(destination / L"install-files.txt"))
        return false;
    std::error_code size_error;
    if (fs::file_size(destination / L"install-files.txt", size_error) > 32768 || size_error)
        return false;
    std::vector<std::wstring> previous;
    {
        std::wifstream manifest(destination / L"install-files.txt");
        std::wstring name;
        while (std::getline(manifest, name)) {
            if (!flat_name(name) || previous.size() >= 100)
                return false;
            previous.push_back(name);
        }
    }
    if (pid) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t name[32768];
            DWORD size = 32768;
            bool same =
                QueryFullProcessImageNameW(process, 0, name, &size) &&
                fs::path(name).lexically_normal() == (destination / L"AirConnect.exe").lexically_normal();
            if (!same || WaitForSingleObject(process, 60000) != WAIT_OBJECT_0) {
                CloseHandle(process);
                return false;
            }
            CloseHandle(process);
        }
    }
    HANDLE active = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\AirConnect.Instance.v1");
    if (active) {
        CloseHandle(active);
        if (!test_only)
            return false;
    }
    auto stage = destination / (L".update-" + std::to_wstring(GetCurrentProcessId()));
    auto backup = stage / L"backup";
    std::error_code ec;
    if (!fs::create_directory(stage, ec) || ec)
        return false;
    bool ok = extract(stage);
    auto files = installed;
    files.push_back(L"install-files.txt");
    auto all = files;
    for (const auto &name : previous)
        if (std::find(all.begin(), all.end(), name) == all.end())
            all.push_back(name);
    std::vector<std::wstring> saved, moved;
    if (ok) {
        fs::create_directory(backup, ec);
        ok = !ec;
    }
    if (ok)
        for (const auto &name : all) {
            auto target = destination / name;
            if (fs::exists(target)) {
                if (!MoveFileExW(target.c_str(), (backup / name).c_str(), MOVEFILE_WRITE_THROUGH)) {
                    ok = false;
                    break;
                }
                saved.push_back(name);
            }
            if (std::find(files.begin(), files.end(), name) != files.end()) {
                if (!MoveFileExW((stage / name).c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                    ok = false;
                    break;
                }
                moved.push_back(name);
            }
        }
    if (!ok) {
        for (const auto &name : moved)
            DeleteFileW((destination / name).c_str());
        for (const auto &name : saved)
            if (!MoveFileExW((backup / name).c_str(), (destination / name).c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;
    }
    // Delete only our flat manifest files in the exclusively created staging directory.
    for (const auto &name : all) {
        DeleteFileW((stage / name).c_str());
        DeleteFileW((backup / name).c_str());
    }
    RemoveDirectoryW(backup.c_str());
    RemoveDirectoryW(stage.c_str());
    return ok;
}
bool uninstall_files(const fs::path &destination) {
    if (!fs::exists(destination / L"AirConnect.exe") || !fs::exists(destination / L"install-files.txt"))
        return false;
    std::wifstream list(destination / L"install-files.txt");
    std::vector<std::wstring> files;
    std::wstring s;
    while (std::getline(list, s) && files.size() < 100) {
        if (!flat_name(s))
            return false;
        files.push_back(s);
    }
    list.close();
    bool ok = true;
    for (auto &f : files)
        if (!DeleteFileW((destination / f).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            ok = false;
    RemoveDirectoryW(destination.c_str());
    if (ok) {
        RegDeleteTreeW(HKEY_CURRENT_USER, uninstall_key);
        HKEY run;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                          KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
            RegDeleteValueW(run, L"AirConnect");
            RegCloseKey(run);
        }
        auto menu = fs::path(known(FOLDERID_Programs)) / L"AirConnect";
        DeleteFileW((menu / L"AirConnect.lnk").c_str());
        DeleteFileW((menu / L"Uninstall AirConnect.lnk").c_str());
        RemoveDirectoryW(menu.c_str());
        DeleteFileW((fs::path(known(FOLDERID_Desktop)) / L"AirConnect.lnk").c_str());
    }
    return ok;
}
HWND child(const wchar_t *type, const wchar_t *title, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowW(type, title, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    switch (m) {
    case WM_CREATE:
        window = w;
        font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                           L"Segoe UI");
        heading = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                              L"Segoe UI");
        {
            HWND title = child(L"STATIC", L"Install AirConnect", 0, 28, 26, 490, 38, 0);
            SendMessageW(title, WM_SETFONT, (WPARAM)heading, TRUE);
        }
        child(L"STATIC", L"Native screen mirroring and audio for Windows", 0, 29, 75, 490, 28, 0);
        child(L"STATIC", L"Install for your Windows account", 0, 29, 123, 490, 24, 0);
        path_edit = child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 29, 153, 490, 30, 1);
        SetWindowTextW(path_edit,
                       (update_mode ? fs::path(update_directory)
                                    : fs::path(known(FOLDERID_LocalAppData)) / L"Programs" / L"AirConnect")
                           .c_str());
        desktop =
            child(L"BUTTON", L"Create a desktop shortcut", BS_AUTOCHECKBOX | WS_TABSTOP, 29, 208, 490, 28, 2);
        child(L"STATIC",
              L"Start Menu shortcuts and an uninstaller are included.\nRecordings and personal settings are "
              L"preserved during uninstall.",
              0, 29, 257, 490, 56, 0);
        progress = child(PROGRESS_CLASSW, L"", 0, 29, 326, 490, 12, 0);
        status_label = child(L"STATIC", L"GPL-3.0 · No accounts or subscriptions", 0, 29, 353, 490, 36, 0);
        install_button = child(L"BUTTON", L"Install", BS_DEFPUSHBUTTON | WS_TABSTOP, 409, 410, 110, 36, 3);
        if (update_mode) {
            SetWindowTextW(install_button, L"Update");
            EnableWindow(path_edit, FALSE);
            PostMessageW(w, WM_COMMAND, 3, 0);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(a) == 4) {
            DestroyWindow(w);
            return 0;
        }
        if (LOWORD(a) == 3) {
            if (busy)
                return 0;
            wchar_t text[32768];
            GetWindowTextW(path_edit, text, 32768);
            fs::path destination(text);
            if (destination.empty() || !destination.is_absolute() || destination == destination.root_path()) {
                MessageBoxW(w, L"Choose an application folder, not a drive root.", L"AirConnect",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            HANDLE active = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\AirConnect.Instance.v1");
            if (active && !update_mode) {
                CloseHandle(active);
                MessageBoxW(w, L"Exit AirConnect from its tray menu before installing.", L"AirConnect",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (active)
                CloseHandle(active);
            bool desk = SendMessageW(desktop, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (update_mode)
                desk = fs::exists(fs::path(known(FOLDERID_Desktop)) / L"AirConnect.lnk");
            busy = true;
            EnableWindow(install_button, FALSE);
            EnableWindow(path_edit, FALSE);
            SetWindowTextW(status_label, L"Installing application files…");
            worker = std::thread([destination, desk, w] {
                HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                bool ok = (update_mode || fs::exists(destination / L"install-files.txt"))
                              ? upgrade(destination, update_mode ? wait_pid : 0)
                              : extract(destination);
                if (ok)
                    ok = registration(destination, desk);
                if (SUCCEEDED(init))
                    CoUninitialize();
                PostMessageW(w, WM_APP + 1, ok ? 1 : 0, 0);
            });
        }
        return 0;
    case WM_APP + 2:
        SendMessageW(progress, PBM_SETPOS, (WPARAM)std::min<WPARAM>(a * 5, 95), 0);
        return 0;
    case WM_APP + 1:
        busy = false;
        if (worker.joinable())
            worker.join();
        EnableWindow(install_button, TRUE);
        EnableWindow(path_edit, TRUE);
        SendMessageW(progress, PBM_SETPOS, a ? 100 : 0, 0);
        SetWindowTextW(status_label,
                       a ? L"AirConnect is installed. Open it from the Start Menu."
                         : L"Installation failed. Close running copies and check the destination.");
        SetWindowTextW(install_button, a ? L"Close" : L"Try again");
        if (a)
            SetWindowLongPtrW(install_button, GWLP_ID, 4);
        if (a && update_mode) {
            wchar_t text[32768];
            GetWindowTextW(path_edit, text, 32768);
            auto app = fs::path(text) / L"AirConnect.exe";
            ShellExecuteW(w, L"open", app.c_str(), nullptr, nullptr, SW_SHOW);
            DestroyWindow(w);
        }
        return 0;
    case WM_CLOSE:
        if (!busy)
            DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (m == WM_COMMAND && LOWORD(a) == 4) {
        DestroyWindow(w);
        return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 1;
    if (argc == 3 && std::wstring(argv[1]) == L"--upgrade-test") {
        auto target = fs::absolute(argv[2]);
        auto base =
            fs::absolute(fs::path(module_path()).parent_path().parent_path() / L"build" / L"upgrade-test");
        result = target == base && upgrade(target, 0, true) ? 0 : 1;
        LocalFree(argv);
        if (SUCCEEDED(init))
            CoUninitialize();
        return result;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--update" && std::wstring(argv[3]) == L"--wait-pid") {
        update_mode = true;
        update_directory = argv[2];
        wait_pid = wcstoul(argv[4], nullptr, 10);
        if (!fs::path(update_directory).is_absolute() || !wait_pid) {
            LocalFree(argv);
            return 1;
        }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--extract-only") {
        result = extract(argv[2]) ? 0 : 1;
        LocalFree(argv);
        if (SUCCEEDED(init))
            CoUninitialize();
        return result;
    }
#ifdef AC_UNINSTALLER
    if (argc == 4 && std::wstring(argv[1]) == L"--finish") {
        DWORD pid = wcstoul(argv[3], nullptr, 10);
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (process) {
            WaitForSingleObject(process, 15000);
            CloseHandle(process);
        }
        result = uninstall_files(argv[2]) ? 0 : 1;
        if (result)
            MessageBoxW(
                nullptr,
                L"Some application files are still in use. Close AirConnect and run the uninstaller again.",
                L"AirConnect", MB_OK | MB_ICONINFORMATION);
    } else if (MessageBoxW(nullptr, L"Remove AirConnect? Recordings and personal settings will be kept.",
                           L"Uninstall AirConnect", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        auto directory = fs::path(module_path()).parent_path();
        wchar_t temp[MAX_PATH], helper[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        GetTempFileNameW(temp, L"acu", 0, helper);
        DeleteFileW(helper);
        std::wstring exe = std::wstring(helper) + L".exe";
        if (CopyFileW(module_path().c_str(), exe.c_str(), TRUE)) {
            std::wstring command = L"\"" + exe + L"\" --finish \"" + directory.wstring() + L"\" " +
                                   std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                result = 0;
            }
        }
    }
#else
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&cc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AirConnect.Setup";
    RegisterClassW(&wc);
    HWND w = CreateWindowW(wc.lpszClassName, L"AirConnect Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                           CW_USEDEFAULT, CW_USEDEFAULT, 570, 505, nullptr, nullptr, instance, nullptr);
    ShowWindow(w, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    result = 0;
#endif
    LocalFree(argv);
    if (font)
        DeleteObject(font);
    if (heading)
        DeleteObject(heading);
    if (SUCCEEDED(init))
        CoUninitialize();
    return result;
}
