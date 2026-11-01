#pragma once
#include <windows.h>
namespace ac {
bool restore_window_state(HWND window, const wchar_t *name, bool allow_maximize = true);
void save_window_state(HWND window, const wchar_t *name, const RECT *normal_override = nullptr);
} // namespace ac
