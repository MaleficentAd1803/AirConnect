#include "core/preferences.h"
#include "video/viewer_geometry.h"
#include "platform/window_state.h"
#include "crypto/identity.h"
#include "airplay/receiver_name.h"
#include <iostream>
#include <filesystem>
#include <fstream>
void require(bool b, const char *s) {
    if (!b) {
        std::cerr << "FAIL " << s << "\n";
        std::exit(1);
    }
}
int main() {
    for (auto source : {ac::viewer::Size{1170, 2532}, ac::viewer::Size{2532, 1170},
                        ac::viewer::Size{1920, 1080}, ac::viewer::Size{640, 360}}) {
        auto size = ac::viewer::fit(source.width, source.height, 1800, 950);
        require(size.width <= 1800 && size.height <= 950, "viewer fits monitor work area");
        require(std::abs(double(size.width) / size.height - double(source.width) / source.height) < .003,
                "phone aspect ratio retained");
        auto pip = ac::viewer::fit(source.width, source.height, 1800, 950, 520);
        require(std::max(pip.width, pip.height) <= 520, "PiP long edge bounded");
    }

    auto dir = std::filesystem::current_path() / L"test-data";
    std::filesystem::create_directories(dir);
    ac::set_data_directory(dir);
    ac::Preferences p;
    p.folder = (dir / L"Felvételek").wstring();
    p.name = "AirConnect — Tamas";
    p.theme = 1;
    p.hardware = false;
    p.resolution = 2560;
    HWND placement = CreateWindowW(L"STATIC", L"Window placement fixture", WS_OVERLAPPEDWINDOW, 180, 160, 640,
                                   480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(placement != nullptr, "window fixture created");
    RECT before{}, after{};
    GetWindowRect(placement, &before);
    ac::save_window_state(placement, L"fixture");
    SetWindowPos(placement, nullptr, 350, 320, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
    require(ac::restore_window_state(placement, L"fixture", false), "placement loaded after restart");
    GetWindowRect(placement, &after);
    require(EqualRect(&before, &after), "window position and normal size restored");
    DestroyWindow(placement);
    require(ac::save_preferences(p, dir / L"settings.ini"), "atomic save");
    require(ac::load_preferences(dir / L"settings.ini") == p, "settings roundtrip Unicode");
    auto first = ap::crypto::Identity::load_or_create(ac::utf8((dir / L"identity.dpapi").wstring()));
    auto second = ap::crypto::Identity::load_or_create(ac::utf8((dir / L"identity.dpapi").wstring()));
    require(first && second, "DPAPI identity created");
    require(first->public_key() == second->public_key(), "DPAPI identity stable");
    require(first->sign((const unsigned char *)"test", 4).size() == 64, "Ed25519 signature");
    std::ifstream f(dir / L"identity.dpapi", std::ios::binary | std::ios::ate);
    require(f.tellg() > 32, "identity encrypted on disk");
    std::ofstream bad(dir / L"invalid.ini");
    bad << "resolution=99999\nfps=-1\ntheme=8\nbitrate=999\nname=\n";
    bad.close();
    auto clamped = ac::load_preferences(dir / L"invalid.ini");
    require(clamped.resolution == 1920 && clamped.fps == 60 && clamped.theme == 2 && clamped.bitrate == 40 &&
                clamped.name == "AirConnect",
            "invalid preferences bounded");
    require(ac::wide(ac::utf8(L"Árvíztűrő tükörfúrógép")) == L"Árvíztűrő tükörfúrógép", "UTF8 conversion");
    std::cout << "6 native settings and identity checks passed\n";
    return 0;
}
