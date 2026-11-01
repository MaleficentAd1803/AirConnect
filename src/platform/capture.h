#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
namespace ac {
bool save_png(const std::wstring&,int,int,const uint8_t*,UINT);
bool capture_own_window(HWND,const std::wstring&);
}
