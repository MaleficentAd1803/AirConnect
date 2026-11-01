#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
namespace ac {
struct ReleaseUpdate {
    std::string version, url, digest;
    uint64_t bytes = 0;
};
bool newer_version(const std::string &candidate, const std::string &current);
ReleaseUpdate parse_release(const std::string &json);
enum class UpdateState { Idle, Checking, Current, Available, Downloading, Ready, Error };
struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    std::wstring message = L"Updates are checked only when you ask.";
    ReleaseUpdate release;
    std::filesystem::path installer;
};
class UpdateService {
  public:
    ~UpdateService();
    void check();
    void download();
    UpdateStatus status() const;

  private:
    void set(UpdateStatus);
    std::thread worker;
    std::atomic<bool> cancel{false};
    mutable std::mutex mutex;
    UpdateStatus current;
};
// Shared by the integration test; downloads the real GitHub asset and checks SHA-256.
ReleaseUpdate latest_release();
ReleaseUpdate latest_release(const std::atomic<bool> &cancel);
void download_release(const ReleaseUpdate &, const std::filesystem::path &, const std::atomic<bool> &cancel);
} // namespace ac
