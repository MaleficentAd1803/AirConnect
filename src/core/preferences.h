#pragma once
#include "airplay/live_settings.h"
#include <filesystem>
#include <string>
#include <vector>
#include <atomic>
#include <windows.h>

namespace ac {
std::wstring wide(const std::string& s);
std::string utf8(const std::wstring& s);
int64_t now_ns();
struct Preferences {
 std::string name="AirConnect";
 int theme=1; // system, AMOLED dark, light
 int resolution=1920, fps=60, bitrate=12, codec=0, encoder=0;
 bool hardware=true, hevc=true, vsync=true, close_to_tray=true, stats=false;
 std::wstring folder, output_device;
 bool operator==(const Preferences&) const = default;
};
std::filesystem::path data_directory();
void set_data_directory(const std::filesystem::path& path);
Preferences load_preferences(const std::filesystem::path& path);
bool save_preferences(const Preferences& p,const std::filesystem::path& path);
void apply_preferences(const Preferences& p,ap::airplay::LiveSettings& s);
bool set_startup(bool enable);
bool startup_enabled();
std::wstring default_recordings();
bool is_dark(int theme);
extern std::atomic<float> playback_volume;
extern std::atomic<bool> playback_muted;
void set_output_device(const std::wstring& id);
std::wstring output_device();
std::vector<std::pair<std::wstring,std::wstring>> audio_devices();
}
