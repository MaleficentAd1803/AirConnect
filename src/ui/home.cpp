#include "ui/home.h"
#include "core/updates.h"
#include "version.h"
#include "core/preferences.h"
#include "airplay/receiver_name.h"
#include "platform/capture.h"
#include "platform/window_state.h"
#include "log.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <fstream>
#include <algorithm>
#include <vector>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace ac {
namespace {
constexpr UINT TrayMessage = WM_APP + 1;
const wchar_t *categories[] = {L"General",   L"Receiver", L"Video",    L"Audio",
                               L"Recording", L"Network",  L"Advanced", L"About"};
std::wstring read_text(HWND c) {
    int n = GetWindowTextLengthW(c);
    std::wstring s(n + 1, 0);
    GetWindowTextW(c, s.data(), n + 1);
    s.resize(n);
    return s;
}
} // namespace
struct Home::Impl {
    Receiver &receiver;
    Preferences prefs;
    HINSTANCE instance{};
    HWND home{}, settings{}, list{}, hovered{};
    HFONT font{}, bold{}, heading{}, symbols{};
    bool closing = false, dark = true;
    int smoke = 0;
    DWORD started = 0;
    UpdateService updates;
    HWND update_label = nullptr, update_button = nullptr;
    int capture_step = 0;
    bool draft_startup = false;
    NOTIFYICONDATAW tray{};
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> target;
    HBRUSH bgbrush = nullptr, fieldbrush = nullptr;
    std::vector<HWND> fields;
    HWND control[20]{};
    int page = 0;
    Preferences draft;
    std::vector<std::pair<std::wstring, std::wstring>> devices;
    Impl(Receiver &r, Preferences p) : receiver(r), prefs(std::move(p)) {}
    D2D1_COLOR_F color(unsigned rgb) {
        return D2D1::ColorF(rgb);
    }
    unsigned bg() const {
        return dark ? 0x000000 : 0xF7F8FA;
    }
    unsigned fg() const {
        return dark ? 0xF1F3F6 : 0x1D2530;
    }
    unsigned muted() const {
        return dark ? 0xA8B0BB : 0x5C6775;
    }
    unsigned line() const {
        return dark ? 0x242424 : 0xDCE1E7;
    }
    void theme() {
        dark = is_dark(prefs.theme);
        if (bgbrush)
            DeleteObject(bgbrush);
        if (fieldbrush)
            DeleteObject(fieldbrush);
        bgbrush = CreateSolidBrush(dark ? RGB(0, 0, 0) : RGB(247, 248, 250));
        fieldbrush = CreateSolidBrush(dark ? RGB(16, 16, 16) : RGB(255, 255, 255));
        BOOL d = dark;
        for (HWND w : {home, settings})
            if (w) {
                DwmSetWindowAttribute(w, 20, &d, sizeof(d));
                COLORREF caption = dark ? RGB(0, 0, 0) : RGB(247, 248, 250);
                DwmSetWindowAttribute(w, 35, &caption, sizeof(caption));
                InvalidateRect(w, nullptr, TRUE);
            }
        target.Reset();
    }
    float scale(HWND w) const {
        return GetDpiForWindow(w) / 96.f;
    }
    void glyph(ID2D1RenderTarget *t, wchar_t code, float x, float y, float size, unsigned rgb) {
        ComPtr<IDWriteTextFormat> f;
        dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &f);
        ComPtr<ID2D1SolidColorBrush> b;
        t->CreateSolidColorBrush(color(rgb), &b);
        t->DrawText(&code, 1, f.Get(), D2D1::RectF(x, y, x + size + 4, y + size + 5), b.Get());
    }
    void receiver_mark(ID2D1RenderTarget *t, float x, float y, float size) {
        ComPtr<ID2D1SolidColorBrush> b;
        t->CreateSolidColorBrush(color(dark ? 0x78ABE8 : 0x246BC5), &b);
        float u = size / 64;
        t->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(x + 3 * u, y + 5 * u, x + 61 * u, y + 46 * u), 4 * u, 4 * u),
            b.Get(), 2 * u);
        t->DrawLine({x + 17 * u, y + 55 * u}, {x + 47 * u, y + 55 * u}, b.Get(), 2 * u);
        t->DrawLine({x + 24 * u, y + 46 * u}, {x + 24 * u, y + 55 * u}, b.Get(), 2 * u);
        t->DrawLine({x + 40 * u, y + 46 * u}, {x + 40 * u, y + 55 * u}, b.Get(), 2 * u);
        t->DrawLine({x + 26 * u, y + 28 * u}, {x + 32 * u, y + 22 * u}, b.Get(), 2 * u);
        t->DrawLine({x + 32 * u, y + 22 * u}, {x + 38 * u, y + 28 * u}, b.Get(), 2 * u);
        t->DrawLine({x + 32 * u, y + 23 * u}, {x + 32 * u, y + 36 * u}, b.Get(), 2 * u);
    }
    void text(ID2D1RenderTarget *t, const std::wstring &s, float x, float y, float w, float h, float size,
              unsigned col, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
        ComPtr<IDWriteTextFormat> f;
        dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &f);
        ComPtr<ID2D1SolidColorBrush> b;
        t->CreateSolidColorBrush(color(col), &b);
        t->DrawText(s.c_str(), (UINT32)s.size(), f.Get(), D2D1::RectF(x, y, x + w, y + h), b.Get());
    }
    void draw_home(ID2D1RenderTarget *t, float w, float h) {
        t->Clear(color(bg()));
        ComPtr<ID2D1SolidColorBrush> b;
        t->CreateSolidColorBrush(color(line()), &b);
        receiver_mark(t, 30, 18, 30);
        text(t, L"AirConnect", 75, 21, w - 255, 32, 19, fg(), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        text(t, L"Wireless screen & audio receiver", 75, 48, w - 255, 21, 12, muted());
        t->DrawLine({32, 86}, {w - 32, 86}, b.Get());
        auto state = receiver.status();
        bool active = state.state == State::Mirroring || state.state == State::Connected ||
                      state.state == State::AudioOnly;
        receiver_mark(t, 34, 133, 64);
        text(t, L"This receiver", 122, 124, w - 170, 24, 12, muted());
        text(t, wide(prefs.name), 120, 149, w - 166, 44, 28, fg(), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        unsigned status = state.state == State::Error || !state.network_notice.empty() ? 0xCE9C55
                          : state.advertised                                           ? 0x69B990
                                                                                       : muted();
        b->SetColor(color(status));
        t->FillEllipse(D2D1::Ellipse({126, 207}, 3.5f, 3.5f), b.Get());
        text(t,
             !state.network_notice.empty() && !active ? L"Advertised · Review network access"
                                                      : state_text(state.state),
             138, 196, w - 190, 25, 14, status);
        b->SetColor(color(line()));
        t->DrawLine({32, 250}, {w - 32, 250}, b.Get());
        if (active) {
            text(t, wide(state.device.empty() ? "Apple device connected" : state.device), 34, 281, w - 70, 32,
                 19, fg(), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            text(t,
                 state.state == State::AudioOnly ? L"Audio is playing through your Windows output."
                                                 : L"Your screen is open in the viewing window.",
                 34, 325, w - 70, 26, 14, muted());
            if (state.video.width)
                text(t,
                     std::to_wstring(state.video.width) + L" × " + std::to_wstring(state.video.height) +
                         L"   " + wide(state.video.codec) + L"   " +
                         (state.video.gpu ? L"D3D11 hardware" : L"Software decoder"),
                     34, 366, w - 70, 25, 13, muted());
        } else if (state.state == State::Error) {
            text(t, L"Let’s check the connection", 34, 276, w - 70, 34, 18, fg(),
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
            text(t, wide(state.message), 34, 320, w - 70, 75, 14, muted());
        } else {
            text(t, L"Connect your iPhone or iPad", 34, 275, w - 70, 31, 18, fg(),
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
            const wchar_t *titles[] = {L"Use the same local network", L"Open Screen Mirroring",
                                       L"Choose your receiver"};
            std::wstring details[] = {L"Connect your device to the network used by this PC.",
                                      L"On your device, open Control Center → Screen Mirroring.",
                                      L"Select “" + wide(prefs.name) + L"” from the list."};
            const wchar_t icons[] = {0xE701, 0xE95A, 0xE8B7};
            for (int i = 0; i < 3; ++i) {
                float y = 326 + i * 57;
                glyph(t, icons[i], 36, y + 4, 19, muted());
                text(t, titles[i], 74, y, w - 110, 23, 14, fg(), DWRITE_FONT_WEIGHT_SEMI_BOLD);
                text(t, details[i], 74, y + 24, w - 110, 24, 12, muted());
            }
        }
        b->SetColor(color(line()));
        t->DrawLine({32, h - 66}, {w - 32, h - 66}, b.Get());
        glyph(t, 0xE968, 34, h - 45, 15, muted());
        text(t, L"Local connection", 60, h - 49, 180, 25, 12, muted());
        if (!state.address.empty())
            text(t, wide(state.address), w - 290, h - 49, 250, 25, 12, muted());
    }
    void paint_home() {
        PAINTSTRUCT ps;
        BeginPaint(home, &ps);
        RECT r{};
        GetClientRect(home, &r);
        float s = scale(home), w = r.right / s, h = r.bottom / s;
        if (!target) {
            D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties();
            rp.dpiX = rp.dpiY = 96 * s;
            d2d->CreateHwndRenderTarget(
                rp, D2D1::HwndRenderTargetProperties(home, D2D1::SizeU(r.right, r.bottom)), &target);
        }
        if (target) {
            target->BeginDraw();
            draw_home(target.Get(), w, h);
            if (target->EndDraw() == D2DERR_RECREATE_TARGET)
                target.Reset();
        }
        EndPaint(home, &ps);
    }
    void print_home(HDC dc) {
        RECT r{};
        GetClientRect(home, &r);
        float s = scale(home);
        ComPtr<ID2D1DCRenderTarget> offscreen;
        auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96 * s, 96 * s);
        HRESULT hr = d2d->CreateDCRenderTarget(&properties, &offscreen);
        if (SUCCEEDED(hr))
            hr = offscreen->BindDC(dc, &r);
        if (SUCCEEDED(hr)) {
            offscreen->BeginDraw();
            draw_home(offscreen.Get(), r.right / s, r.bottom / s);
            hr = offscreen->EndDraw();
        }
        if (FAILED(hr))
            LOG_ERROR << "UI capture render HRESULT=" << std::hex << (unsigned)hr;
    }
    void capture_home(const std::wstring &path) {
        RECT r{};
        GetClientRect(home, &r);
        HDC dc = GetDC(home), mem = CreateCompatibleDC(dc);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = r.right;
        info.bmiHeader.biHeight = -r.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        auto old = SelectObject(mem, bmp);
        print_home(mem);
        struct ChildCapture {
            HWND root;
            HDC dc;
        } capture{home, mem};
        EnumChildWindows(
            home,
            [](HWND child, LPARAM data) -> BOOL {
                if (!IsWindowVisible(child))
                    return TRUE;
                auto *c = (ChildCapture *)data;
                RECT r{};
                GetWindowRect(child, &r);
                MapWindowPoints(nullptr, c->root, (POINT *)&r, 2);
                int saved = SaveDC(c->dc);
                SetViewportOrgEx(c->dc, r.left, r.top, nullptr);
                IntersectClipRect(c->dc, 0, 0, r.right - r.left, r.bottom - r.top);
                SendMessageW(child, WM_PRINT, (WPARAM)c->dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
                RestoreDC(c->dc, saved);
                return TRUE;
            },
            (LPARAM)&capture);
        GdiFlush();
        if (bits) {
            auto *b = (uint8_t *)bits;
            for (size_t i = 3; i < (size_t)r.right * r.bottom * 4; i += 4)
                b[i] = 255;
            save_png(path, r.right, r.bottom, b, r.right * 4);
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(home, dc);
    }
    HWND button(HWND parent, int id, const wchar_t *label, int x, int y, int w = 100) {
        HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, w,
                               34, parent, (HMENU)(INT_PTR)id, instance, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        SetWindowSubclass(c, control_proc, 1, (DWORD_PTR)this);
        return c;
    }
    void draw_button(DRAWITEMSTRUCT *d) {
        RECT r = d->rcItem;
        bool down = (d->itemState & ODS_SELECTED) != 0, over = hovered == d->hwndItem,
             primary = d->CtlID == 201 || d->CtlID == 11;
        COLORREF fill = primary ? (down   ? RGB(31, 83, 151)
                                   : over ? RGB(54, 126, 218)
                                          : RGB(36, 107, 197))
                        : dark  ? (down   ? RGB(34, 34, 34)
                                   : over ? RGB(29, 29, 29)
                                          : RGB(17, 17, 17))
                                : (down   ? RGB(224, 231, 240)
                                   : over ? RGB(236, 240, 246)
                                          : RGB(255, 255, 255));
        FillRect(d->hDC, &r, bgbrush);
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : dark ? RGB(48, 48, 48) : RGB(202, 210, 220));
        auto oldb = SelectObject(d->hDC, brush), oldp = SelectObject(d->hDC, pen);
        RoundRect(d->hDC, r.left, r.top, r.right - 1, r.bottom - 1, 8, 8);
        SelectObject(d->hDC, oldb);
        SelectObject(d->hDC, oldp);
        DeleteObject(brush);
        DeleteObject(pen);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, primary || dark ? RGB(242, 245, 250) : RGB(38, 50, 67));
        SelectObject(d->hDC, font);
        auto label = read_text(d->hwndItem);
        DrawTextW(d->hDC, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d->itemState & ODS_FOCUS) {
            InflateRect(&r, -4, -4);
            DrawFocusRect(d->hDC, &r);
        }
    }
    void draw_navigation(DRAWITEMSTRUCT *d) {
        if (d->itemID == (UINT)-1)
            return;
        RECT r = d->rcItem;
        bool selected = d->itemState & ODS_SELECTED;
        COLORREF back = dark ? (selected ? RGB(13, 25, 41) : RGB(0, 0, 0))
                             : (selected ? RGB(225, 235, 249) : RGB(239, 242, 246));
        HBRUSH b = CreateSolidBrush(back);
        FillRect(d->hDC, &r, b);
        DeleteObject(b);
        if (selected) {
            RECT marker{r.left, r.top + 9, r.left + 3, r.bottom - 9};
            b = CreateSolidBrush(RGB(52, 127, 214));
            FillRect(d->hDC, &marker, b);
            DeleteObject(b);
        }
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, selected ? (dark ? RGB(142, 190, 248) : RGB(31, 91, 166))
                                      : (dark ? RGB(173, 182, 195) : RGB(91, 103, 120)));
        const wchar_t icons[] = {0xE713, 0xE95A, 0xE714, 0xE767, 0xE7C8, 0xE701, 0xE9CE, 0xE946};
        SelectObject(d->hDC, symbols);
        RECT icon = r;
        icon.left += 15;
        icon.right = icon.left + 28;
        DrawTextW(d->hDC, &icons[d->itemID], 1, &icon, DT_SINGLELINE | DT_VCENTER);
        SelectObject(d->hDC, selected ? bold : font);
        r.left += 49;
        DrawTextW(d->hDC, categories[d->itemID], -1, &r, DT_SINGLELINE | DT_VCENTER);
    }
    void draw_combo(DRAWITEMSTRUCT *d) {
        RECT r = d->rcItem;
        HBRUSH b =
            CreateSolidBrush(d->itemState & ODS_SELECTED ? (dark ? RGB(16, 36, 60) : RGB(226, 237, 251))
                                                         : (dark ? RGB(16, 16, 16) : RGB(255, 255, 255)));
        FillRect(d->hDC, &r, b);
        DeleteObject(b);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, dark ? RGB(232, 237, 244) : RGB(38, 50, 67));
        SelectObject(d->hDC, font);
        r.left += 11;
        if (d->itemID != (UINT)-1) {
            auto len = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
            std::wstring value((size_t)std::max<LRESULT>(len, 0) + 1, 0);
            SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)value.data());
            DrawTextW(d->hDC, value.c_str(), -1, &r, DT_VCENTER | DT_SINGLELINE);
        }
    }
    static LRESULT CALLBACK control_proc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR kind, DWORD_PTR data) {
        auto *p = (Impl *)data;
        if (m == WM_MOUSEMOVE) {
            if (p->hovered != w) {
                p->hovered = w;
                InvalidateRect(w, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0};
            TrackMouseEvent(&track);
        }
        if (m == WM_MOUSELEAVE) {
            p->hovered = nullptr;
            InvalidateRect(w, nullptr, FALSE);
        }
        if (m == WM_SETFOCUS || m == WM_KILLFOCUS)
            InvalidateRect(w, nullptr, FALSE);
        if (kind == 3 && m == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            RECT r;
            GetClientRect(w, &r);
            HBRUSH brush = CreateSolidBrush(p->dark ? RGB(16, 16, 16) : RGB(255, 255, 255));
            HPEN pen = CreatePen(PS_SOLID, 1,
                                 GetFocus() == w ? RGB(52, 127, 214)
                                 : p->dark       ? RGB(46, 46, 46)
                                                 : RGB(195, 204, 216));
            auto oldb = SelectObject(dc, brush), oldp = SelectObject(dc, pen);
            RoundRect(dc, 0, 0, r.right - 1, r.bottom - 1, 7, 7);
            SelectObject(dc, oldb);
            SelectObject(dc, oldp);
            DeleteObject(brush);
            DeleteObject(pen);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, p->dark ? RGB(232, 237, 244) : RGB(38, 50, 67));
            SelectObject(dc, p->font);
            RECT label = r;
            label.left += 12;
            label.right -= 34;
            auto value = read_text(w);
            DrawTextW(dc, value.c_str(), -1, &label, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, p->symbols);
            label = r;
            label.left = r.right - 27;
            wchar_t arrow = 0xE70D;
            DrawTextW(dc, &arrow, 1, &label, DT_VCENTER | DT_SINGLELINE);
            EndPaint(w, &ps);
            return 0;
        }
        if (kind == 2 && m == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            RECT r;
            GetClientRect(w, &r);
            FillRect(dc, &r, p->bgbrush);
            int s = MulDiv(20, GetDpiForWindow(w), 96);
            bool on = SendMessageW(w, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int yy = (r.bottom - s) / 2;
            HBRUSH brush = CreateSolidBrush(on        ? RGB(52, 127, 214)
                                            : p->dark ? RGB(53, 53, 53)
                                                      : RGB(190, 198, 209));
            auto old = SelectObject(dc, brush);
            auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, 0, yy, s * 2, yy + s, s, s);
            SelectObject(dc, old);
            DeleteObject(brush);
            brush = CreateSolidBrush(RGB(250, 252, 255));
            old = SelectObject(dc, brush);
            int x = on ? s + 3 : 3;
            Ellipse(dc, x, yy + 3, x + s - 6, yy + s - 3);
            SelectObject(dc, old);
            SelectObject(dc, pen);
            DeleteObject(brush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, p->dark ? RGB(233, 237, 244) : RGB(35, 46, 62));
            SelectObject(dc, p->font);
            r.left = s * 2 + 12;
            auto value = read_text(w);
            DrawTextW(dc, value.c_str(), -1, &r, DT_VCENTER | DT_SINGLELINE);
            if (GetFocus() == w) {
                InflateRect(&r, -2, -3);
                DrawFocusRect(dc, &r);
            }
            EndPaint(w, &ps);
            return 0;
        }
        return DefSubclassProc(w, m, a, b);
    }
    void layout_home() {
        RECT r{};
        GetClientRect(home, &r);
        float s = scale(home);
        MoveWindow(GetDlgItem(home, 10), (int)(r.right - 124 * s), (int)(29 * s), (int)(96 * s),
                   (int)(34 * s), TRUE);
        auto state = receiver.status();
        bool active = state.state == State::Mirroring || state.state == State::Connected ||
                      state.state == State::AudioOnly;
        HWND viewer = GetDlgItem(home, 11), disconnect = GetDlgItem(home, 12), retry = GetDlgItem(home, 13);
        for (HWND b : {viewer, disconnect, retry})
            ShowWindow(b, SW_HIDE);
        if (active) {
            MoveWindow(viewer, (int)(34 * s), (int)(410 * s), (int)(145 * s), (int)(34 * s), TRUE);
            ShowWindow(viewer, SW_SHOW);
            MoveWindow(disconnect, (int)(196 * s), (int)(410 * s), (int)(108 * s), (int)(34 * s), TRUE);
            ShowWindow(disconnect, SW_SHOW);
        }
        if (state.state == State::Error) {
            MoveWindow(retry, (int)(34 * s), (int)(410 * s), (int)(145 * s), (int)(34 * s), TRUE);
            ShowWindow(retry, SW_SHOW);
        }
    }
    void capture_test_step() {
        if (smoke < 20 || GetTickCount() - started < 3000)
            return;
        auto dir = data_directory() / L"ui-captures";
        std::filesystem::create_directories(dir);
        if (capture_step == 0) {
            capture_home((dir / L"home-system.png").wstring());
            open_settings();
        } else if (capture_step >= 1 && capture_step <= 8) {
            page = capture_step - 1;
            SendMessageW(list, LB_SETCURSEL, page, 0);
            build_page();
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            capture_own_window(settings, (dir / (std::to_wstring(page) + L"-settings.png")).wstring());
        } else if (capture_step == 9) {
            if (settings)
                DestroyWindow(settings);
            prefs.theme = 2;
            theme();
            RedrawWindow(home, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            capture_home((dir / L"home-light.png").wstring());
        } else if (capture_step == 10) {
            prefs.theme = 1;
            theme();
            RedrawWindow(home, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            capture_home((dir / L"home-dark.png").wstring());
        } else if (capture_step == 11)
            open_settings();
        else if (capture_step >= 12 && capture_step <= 19) {
            page = capture_step - 12;
            SendMessageW(list, LB_SETCURSEL, page, 0);
            build_page();
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            capture_own_window(settings, (dir / (std::to_wstring(page) + L"-dark-settings.png")).wstring());
        } else if (capture_step == 20 && settings)
            DestroyWindow(settings);
        ++capture_step;
    }
    void show_home() {
        ShowWindow(home, SW_RESTORE);
        SetForegroundWindow(home);
    }
    void tray_menu() {
        HMENU menu = CreatePopupMenu();
        auto status = receiver.status();
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 1, state_text(status.state));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 2, L"Open AirConnect");
        AppendMenuW(menu, MF_STRING, 3, L"Settings");
        AppendMenuW(menu, MF_STRING, 4, L"Disconnect");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 5, L"Exit");
        POINT point;
        GetCursorPos(&point);
        SetForegroundWindow(home);
        int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, home, nullptr);
        DestroyMenu(menu);
        if (id == 2)
            show_home();
        else if (id == 3)
            open_settings();
        else if (id == 4)
            receiver.disconnect();
        else if (id == 5) {
            closing = true;
            DestroyWindow(home);
        }
    }
    void clear_fields() {
        for (HWND c : fields)
            DestroyWindow(c);
        fields.clear();
        std::fill(std::begin(control), std::end(control), nullptr);
    }
    HWND field(const wchar_t *klass, int id, const wchar_t *value, int x, int y, int w, int h,
               DWORD style = 0) {
        float s = scale(settings);
        HWND c = CreateWindowExW((std::wstring(klass) == L"EDIT") ? WS_EX_CLIENTEDGE : 0, klass, value,
                                 WS_CHILD | WS_VISIBLE | style, (int)(x * s), (int)(y * s), (int)(w * s),
                                 (int)(h * s), settings, (HMENU)(INT_PTR)id, instance, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        fields.push_back(c);
        return c;
    }
    void label(const wchar_t *t, int y, int h = 26) {
        field(L"STATIC", 0, t, 218, y, 520, h, SS_LEFT);
    }
    HWND combo(int idx, int y, const std::vector<std::wstring> &values, int selected, int width = 330) {
        HWND c = field(L"COMBOBOX", 300 + idx, L"", 218, y, width, 280,
                       WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL);
        SetWindowSubclass(c, control_proc, 3, (DWORD_PTR)this);
        SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)(30 * scale(settings)));
        SendMessageW(c, CB_SETITEMHEIGHT, 0, (LPARAM)(28 * scale(settings)));
        for (auto &v : values)
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)v.c_str());
        SendMessageW(c, CB_SETCURSEL, selected, 0);
        control[idx] = c;
        return c;
    }
    HWND check(int idx, int y, const wchar_t *label, bool on) {
        HWND c = field(L"BUTTON", 300 + idx, label, 218, y, 510, 28, WS_TABSTOP | BS_AUTOCHECKBOX);
        SetWindowSubclass(c, control_proc, 2, (DWORD_PTR)this);
        SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        control[idx] = c;
        return c;
    }
    int selection(int i) {
        return (int)SendMessageW(control[i], CB_GETCURSEL, 0, 0);
    }
    bool checked(int i) {
        return SendMessageW(control[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void collect_page() {
        switch (page) {
        case 0:
            draft_startup = checked(2);
            draft.theme = selection(0);
            draft.close_to_tray = checked(1);
            break;
        case 1: {
            auto n = ap::airplay::normalize_receiver_name(utf8(read_text(control[0])));
            if (!n.empty())
                draft.name = n;
            break;
        }
        case 2: {
            int res[] = {1280, 1920, 2560, 3840};
            draft.resolution = res[std::clamp(selection(0), 0, 3)];
            draft.fps = selection(1) == 0 ? 30 : 60;
            draft.hardware = checked(2);
            draft.hevc = checked(3);
            draft.vsync = checked(4);
            break;
        }
        case 3:
            if (selection(0) >= 0 && selection(0) < (int)devices.size())
                draft.output_device = devices[selection(0)].first;
            break;
        case 4: {
            draft.folder = read_text(control[0]);
            draft.codec = selection(1);
            draft.encoder = selection(2);
            int rates[] = {4, 8, 12, 20, 40};
            draft.bitrate = rates[std::clamp(selection(3), 0, 4)];
            break;
        }
        case 6:
            draft.stats = checked(0);
            break;
        }
    }
    void build_page() {
        clear_fields();
        label(categories[page], 26, 34);
        SendMessageW(fields.back(), WM_SETFONT, (WPARAM)heading, TRUE);
        switch (page) {
        case 0:
            label(L"Appearance", 80);
            combo(0, 108, {L"Follow Windows", L"AMOLED dark", L"Light"}, draft.theme);
            check(1, 166, L"Keep receiving when the main window closes", draft.close_to_tray);
            check(2, 209, L"Start AirConnect with Windows", draft_startup);
            label(L"Closing to the tray keeps the receiver available.\nUse Exit in the tray menu to stop "
                  L"receiving.",
                  271, 68);
            break;
        case 1:
            label(L"Receiver name", 80);
            control[0] =
                field(L"EDIT", 300, wide(draft.name).c_str(), 218, 110, 380, 32, WS_TABSTOP | ES_AUTOHSCROLL);
            SendMessageW(control[0], EM_SETLIMITTEXT, 50, 0);
            label(L"This name appears in Screen Mirroring on your Apple device.\nThe receiver name updates "
                  L"when you apply changes.",
                  163, 64);
            label(L"Access", 262);
            label(L"Receives from devices on your local network.\nPIN access control is not supported by "
                  L"this protocol foundation.\nUse a trusted private network.",
                  297, 90);
            break;
        case 2: {
            label(L"Maximum stream size", 80);
            int res[] = {1280, 1920, 2560, 3840}, sel = 1;
            for (int i = 0; i < 4; ++i)
                if (res[i] == draft.resolution)
                    sel = i;
            combo(0, 108, {L"1280 × 1280", L"1920 × 1920", L"2560 × 2560", L"3840 × 3840"}, sel, 230);
            label(L"Maximum frame rate", 160);
            combo(1, 187, {L"30 fps", L"60 fps"}, draft.fps == 60 ? 1 : 0, 230);
            check(2, 240, L"Use hardware video decoding when available", draft.hardware);
            check(3, 279, L"Allow HEVC video", draft.hevc);
            check(4, 318, L"Synchronize presentation to the display", draft.vsync);
            label(L"Reconnect your device after changing stream options.\nThe sender chooses the actual "
                  L"resolution and frame rate.",
                  377, 62);
            break;
        }
        case 3: {
            label(L"Output device", 80);
            devices = audio_devices();
            std::vector<std::wstring> names;
            int sel = 0;
            for (size_t i = 0; i < devices.size(); ++i) {
                names.push_back(devices[i].second);
                if (devices[i].first == draft.output_device)
                    sel = (int)i;
            }
            combo(0, 108, names, sel, 490);
            label(L"Playback volume", 174);
            control[1] = field(TRACKBAR_CLASSW, 301, L"", 218, 206, 420, 40, WS_TABSTOP | TBS_AUTOTICKS);
            SendMessageW(control[1], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(control[1], TBM_SETPOS, TRUE, (int)(playback_volume * 100));
            check(2, 265, L"Mute playback", playback_muted);
            label(L"Volume and mute affect playback only.\nRecordings use the incoming audio level.", 331,
                  62);
            break;
        }
        case 4: {
            label(L"Save recordings and screenshots to", 80);
            control[0] =
                field(L"EDIT", 300, draft.folder.c_str(), 218, 108, 420, 32, WS_TABSTOP | ES_AUTOHSCROLL);
            field(L"BUTTON", 450, L"Browse…", 648, 108, 88, 32, WS_TABSTOP | BS_PUSHBUTTON);
            label(L"Video codec", 160);
            combo(1, 187, {L"H.264", L"HEVC"}, draft.codec, 200);
            label(L"Encoder", 235);
            combo(2, 262, {L"Automatic", L"Hardware only", L"Software"}, draft.encoder, 200);
            label(L"Video bitrate", 310);
            int rates[] = {4, 8, 12, 20, 40}, sel = 2;
            for (int i = 0; i < 5; ++i)
                if (rates[i] == draft.bitrate)
                    sel = i;
            combo(3, 337, {L"4 Mbps", L"8 Mbps", L"12 Mbps", L"20 Mbps", L"40 Mbps"}, sel, 200);
            label(L"Recording starts only when you choose Record.\nChanges apply to the next recording. MP4 "
                  L"includes received audio.",
                  401, 60);
            break;
        }
        case 5: {
            auto s = receiver.status();
            label(L"Local network", 80);
            label(wide(s.address + "   TCP " + std::to_string(s.port)).c_str(), 114);
            label(s.network_notice.empty()
                      ? (s.advertised ? L"Windows confirmed AirPlay and audio advertisements."
                                      : L"Windows has not confirmed discovery registration.")
                      : wide(s.network_notice).c_str(),
                  157, 64);
            label(L"If the receiver does not appear", 233);
            label(L"• Use the same local network on both devices.\n• Check for guest-network isolation or a "
                  L"VPN.\n• Allow AirConnect through Windows Firewall on Private networks.\n• Keep "
                  L"public-facing ports closed.",
                  269, 112);
            field(L"BUTTON", 451, L"Retry discovery", 218, 409, 160, 34, WS_TABSTOP | BS_PUSHBUTTON);
            break;
        }
        case 6:
            check(0, 82, L"Show compact video statistics", draft.stats);
            label(
                L"Statistics report received, decoded and presented frames.\nLocal processing latency starts "
                L"when a frame arrives at the PC;\nit does not include capture or network transit time.",
                139, 90);
            label(L"Diagnostics", 264);
            label(L"Logs stay on this PC. No telemetry is collected.\nPairing keys and raw media packets are "
                  L"excluded from logs.",
                  301, 65);
            field(L"BUTTON", 452, L"Open log folder", 218, 397, 160, 34, WS_TABSTOP | BS_PUSHBUTTON);
            break;
        case 7:
            label(L"AirConnect " AC_VERSION_W, 83);
            label(L"Native Windows AirPlay receiver\nWin32 · Direct3D 11 · WASAPI · FFmpeg", 124, 62);
            label(L"GNU General Public License v3.0\nBased on AirPlay-Windows and UxPlay.\nSettings and "
                  L"recordings are preserved when updating.",
                  220, 84);
            update_label = field(L"STATIC", 454, updates.status().message.c_str(), 218, 320, 490, 54);
            update_button =
                field(L"BUTTON", 455, L"Check for updates", 218, 385, 190, 34, WS_TABSTOP | BS_OWNERDRAW);
            field(L"BUTTON", 453, L"Open documentation", 218, 433, 190, 34, WS_TABSTOP | BS_OWNERDRAW);
            refresh_updates();
            break;
        }
        InvalidateRect(settings, nullptr, TRUE);
    }
    void refresh_updates() {
        if (!settings || page != 7 || !IsWindow(update_label) || !IsWindow(update_button))
            return;
        auto s = updates.status();
        SetWindowTextW(update_label, s.message.c_str());
        const wchar_t *text = s.state == UpdateState::Available     ? L"Download update"
                              : s.state == UpdateState::Ready       ? L"Install and restart"
                              : s.state == UpdateState::Checking    ? L"Checking…"
                              : s.state == UpdateState::Downloading ? L"Downloading…"
                                                                    : L"Check for updates";
        SetWindowTextW(update_button, text);
        EnableWindow(update_button, s.state != UpdateState::Checking && s.state != UpdateState::Downloading);
    }
    void update_action() {
        auto s = updates.status();
        if (s.state == UpdateState::Available)
            updates.download();
        else if (s.state == UpdateState::Ready) {
            wchar_t exe[32768];
            GetModuleFileNameW(nullptr, exe, 32768);
            auto dir = std::filesystem::path(exe).parent_path();
            if (!std::filesystem::exists(dir / L"install-files.txt")) {
                MessageBoxW(
                    settings,
                    L"For a portable update, close AirConnect and extract the new portable ZIP into your "
                    L"application folder. Settings and recordings are kept in your Windows profile.",
                    L"Portable update", MB_OK | MB_ICONINFORMATION);
                ShellExecuteW(settings, L"open",
                              L"https://github.com/MaleficentAd1803/AirConnect/releases/latest", nullptr,
                              nullptr, SW_SHOW);
                return;
            }
            if (MessageBoxW(
                    settings,
                    L"Install the update and restart AirConnect?\nThe current connection will end and any "
                    L"recording will be finalized.\nYour settings and recordings will be kept.",
                    L"Update AirConnect", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
                return;
            std::wstring args =
                L"--update \"" + dir.wstring() + L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId());
            auto result =
                ShellExecuteW(settings, L"open", s.installer.c_str(), args.c_str(), nullptr, SW_SHOW);
            if ((INT_PTR)result > 32) {
                closing = true;
                DestroyWindow(home);
            } else
                MessageBoxW(settings, L"The installer could not be started. Try again.", L"AirConnect",
                            MB_OK | MB_ICONERROR);
        } else
            updates.check();
        refresh_updates();
    }
    void browse() {
        ComPtr<IFileOpenDialog> d;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(d.GetAddressOf()))))
            return;
        DWORD flags = 0;
        d->GetOptions(&flags);
        d->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(d->Show(settings))) {
            ComPtr<IShellItem> i;
            PWSTR s = nullptr;
            if (SUCCEEDED(d->GetResult(&i)) && SUCCEEDED(i->GetDisplayName(SIGDN_FILESYSPATH, &s))) {
                SetWindowTextW(control[0], s);
                CoTaskMemFree(s);
            }
        }
    }
    void apply() {
        collect_page();
        bool autostart = draft_startup;
        if (draft.folder.empty()) {
            MessageBoxW(settings, L"Choose a folder for recordings and screenshots.", L"Recording folder",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (!save_preferences(draft, data_directory() / L"settings.ini")) {
            MessageBoxW(settings, L"Settings could not be saved. Check the application data folder.",
                        L"AirConnect", MB_OK | MB_ICONERROR);
            return;
        }
        if (autostart != startup_enabled() && !set_startup(autostart))
            MessageBoxW(settings, L"Windows did not allow the startup option to be changed.", L"AirConnect",
                        MB_OK | MB_ICONINFORMATION);
        prefs = draft;
        receiver.configure(prefs);
        theme();
        DestroyWindow(settings);
    }
    void open_settings() {
        if (settings) {
            SetForegroundWindow(settings);
            return;
        }
        draft = prefs;
        draft_startup = startup_enabled();
        page = 0;
        settings = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AirConnect.Settings", L"AirConnect Settings",
                                   WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, 0, 780, 590, home, nullptr,
                                   instance, this);
        if (!settings)
            return;
        float s = scale(home);
        RECT rect{};
        GetWindowRect(home, &rect);
        SetWindowPos(settings, nullptr, rect.left + 25, rect.top + 15, (int)(780 * s), (int)(590 * s),
                     SWP_NOZORDER);
        BOOL d = dark;
        DwmSetWindowAttribute(settings, 20, &d, sizeof(d));
        COLORREF caption = dark ? RGB(0, 0, 0) : RGB(247, 248, 250);
        DwmSetWindowAttribute(settings, 35, &caption, sizeof(caption));
        EnableWindow(home, FALSE);
        ShowWindow(settings, SW_SHOW);
    }
    static LRESULT CALLBACK settings_proc(HWND w, UINT m, WPARAM a, LPARAM b) {
        auto *p = (Impl *)GetWindowLongPtrW(w, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            p = (Impl *)((CREATESTRUCTW *)b)->lpCreateParams;
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)p);
            p->settings = w;
        }
        if (!p)
            return DefWindowProcW(w, m, a, b);
        switch (m) {
        case WM_CREATE: {
            float s = p->scale(w);
            p->list = CreateWindowW(L"LISTBOX", nullptr,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS |
                                        LBS_OWNERDRAWFIXED,
                                    12 * s, 25 * s, 164 * s, 432 * s, w, (HMENU)200, p->instance, nullptr);
            SendMessageW(p->list, WM_SETFONT, (WPARAM)p->font, TRUE);
            SendMessageW(p->list, LB_SETITEMHEIGHT, 0, (LPARAM)(42 * s));
            for (auto t : categories)
                SendMessageW(p->list, LB_ADDSTRING, 0, (LPARAM)t);
            SendMessageW(p->list, LB_SETCURSEL, 0, 0);
            p->button(w, 201, L"Apply", (int)(542 * s), (int)(504 * s), (int)(90 * s));
            p->button(w, 202, L"Cancel", (int)(645 * s), (int)(504 * s), (int)(90 * s));
            p->build_page();
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(a) == 200 && HIWORD(a) == LBN_SELCHANGE) {
                p->collect_page();
                p->page = (int)SendMessageW(p->list, LB_GETCURSEL, 0, 0);
                p->build_page();
            } else if (LOWORD(a) == 201)
                p->apply();
            else if (LOWORD(a) == 202)
                DestroyWindow(w);
            else if (LOWORD(a) == 450)
                p->browse();
            else if (LOWORD(a) == 451)
                p->receiver.restart_discovery();
            else if (LOWORD(a) == 452)
                ShellExecuteW(w, L"open", data_directory().c_str(), nullptr, nullptr, SW_SHOW);
            else if (LOWORD(a) == 455)
                p->update_action();
            else if (LOWORD(a) == 453) {
                wchar_t exe[32768];
                GetModuleFileNameW(nullptr, exe, 32768);
                auto path = std::filesystem::path(exe).parent_path() / L"README.md";
                ShellExecuteW(w, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
            } else if (p->page == 3 && LOWORD(a) == 302)
                playback_muted = p->checked(2);
            return 0;
        case WM_HSCROLL:
            if (p->page == 3 && p->control[1])
                playback_volume = (float)SendMessageW(p->control[1], TBM_GETPOS, 0, 0) / 100.f;
            return 0;
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            p->fields.clear();
            p->settings = nullptr;
            EnableWindow(p->home, TRUE);
            SetForegroundWindow(p->home);
            return 0;
        case WM_DRAWITEM: {
            auto *draw = (DRAWITEMSTRUCT *)b;
            if (draw->CtlID == 200)
                p->draw_navigation(draw);
            else if (draw->CtlType == ODT_COMBOBOX)
                p->draw_combo(draw);
            else
                p->draw_button(draw);
            return TRUE;
        }
        case WM_MEASUREITEM: {
            auto *measure = (MEASUREITEMSTRUCT *)b;
            measure->itemHeight = (UINT)(32 * p->scale(w));
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetBkColor((HDC)a, p->dark ? RGB(0, 0, 0) : RGB(247, 248, 250));
            SetTextColor((HDC)a, p->dark ? RGB(235, 239, 245) : RGB(29, 37, 48));
            return (LRESULT)p->bgbrush;
        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(w, &r);
            FillRect((HDC)a, &r, p->bgbrush);
            RECT rail = r;
            rail.right = (LONG)(187 * p->scale(w));
            HBRUSH brush = CreateSolidBrush(p->dark ? RGB(0, 0, 0) : RGB(239, 242, 246));
            FillRect((HDC)a, &rail, brush);
            DeleteObject(brush);
            return 1;
        }
        }
        return DefWindowProcW(w, m, a, b);
    }
    static LRESULT CALLBACK home_proc(HWND w, UINT m, WPARAM a, LPARAM b) {
        auto *p = (Impl *)GetWindowLongPtrW(w, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            p = (Impl *)((CREATESTRUCTW *)b)->lpCreateParams;
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)p);
            p->home = w;
        }
        if (!p)
            return DefWindowProcW(w, m, a, b);
        switch (m) {
        case WM_CREATE:
            p->button(w, 10, L"Settings", 0, 0);
            p->button(w, 11, L"Open viewer", 0, 0, 145);
            p->button(w, 12, L"Disconnect", 0, 0, 110);
            p->button(w, 13, L"Retry discovery", 0, 0, 145);
            SetTimer(w, 1, 500, nullptr);
            p->started = GetTickCount();
            return 0;
        case WM_TIMER: {
            p->refresh_updates();
            p->capture_test_step();
            p->layout_home();
            InvalidateRect(w, nullptr, FALSE);
            auto s = p->receiver.status();
            std::wstring tip = L"AirConnect — " + std::wstring(state_text(s.state));
            wcsncpy_s(p->tray.szTip, 128, tip.c_str(), _TRUNCATE);
            Shell_NotifyIconW(NIM_MODIFY, &p->tray);
            if (p->smoke > 0 && GetTickCount() - p->started > (DWORD)p->smoke * 1000) {
                p->closing = true;
                DestroyWindow(w);
            }
            return 0;
        }
        case WM_PAINT:
            p->paint_home();
            return 0;
        case WM_PRINTCLIENT:
            p->print_home((HDC)a);
            return 0;
        case WM_SIZE:
            if (p->target)
                p->target->Resize(D2D1::SizeU(LOWORD(b), HIWORD(b)));
            p->layout_home();
            return 0;
        case WM_DPICHANGED: {
            auto r = (RECT *)b;
            SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            p->target.Reset();
            p->layout_home();
            return 0;
        }
        case WM_SETTINGCHANGE:
            p->theme();
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO *)b)->ptMinTrackSize = {720, 640};
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM: {
            auto *draw = (DRAWITEMSTRUCT *)b;
            if (draw->CtlID == 200)
                p->draw_navigation(draw);
            else if (draw->CtlType == ODT_COMBOBOX)
                p->draw_combo(draw);
            else
                p->draw_button(draw);
            return TRUE;
        }
        case WM_MEASUREITEM: {
            auto *measure = (MEASUREITEMSTRUCT *)b;
            measure->itemHeight = (UINT)(32 * p->scale(w));
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(a) == 10)
                p->open_settings();
            else if (LOWORD(a) == 11)
                p->receiver.renderer().request_show();
            else if (LOWORD(a) == 12)
                p->receiver.disconnect();
            else if (LOWORD(a) == 13)
                p->receiver.restart_discovery();
            return 0;
        case WM_KEYDOWN:
            if (a == VK_F1)
                p->open_settings();
            return 0;
        case TrayMessage:
            if (LOWORD(b) == WM_LBUTTONDBLCLK)
                p->show_home();
            else if (LOWORD(b) == WM_RBUTTONUP || LOWORD(b) == WM_CONTEXTMENU)
                p->tray_menu();
            return 0;
        case WM_POWERBROADCAST:
            if (a == PBT_APMRESUMEAUTOMATIC)
                p->receiver.restart_discovery();
            return TRUE;
        case WM_CLOSE:
            if (p->prefs.close_to_tray && !p->closing)
                ShowWindow(w, SW_HIDE);
            else
                DestroyWindow(w);
            return 0;
        case WM_EXITSIZEMOVE:
            save_window_state(w, L"home");
            return 0;
        case WM_DESTROY:
            save_window_state(w, L"home");
            if (p->settings)
                DestroyWindow(p->settings);
            Shell_NotifyIconW(NIM_DELETE, &p->tray);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(w, m, a, b);
    }
};
Home::Home(Receiver &r, Preferences p) : p(std::make_unique<Impl>(r, std::move(p))) {}
Home::~Home() {
    if (p->font)
        DeleteObject(p->font);
    if (p->bold)
        DeleteObject(p->bold);
    if (p->heading)
        DeleteObject(p->heading);
    if (p->symbols)
        DeleteObject(p->symbols);
    if (p->bgbrush)
        DeleteObject(p->bgbrush);
    if (p->fieldbrush)
        DeleteObject(p->fieldbrush);
}
void Home::show_settings() {
    p->open_settings();
}
int Home::run(HINSTANCE i, bool background, int smoke) {
    p->instance = i;
    p->smoke = smoke;
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&cc);
    p->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p->bold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p->heading = CreateFontW(-23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p->symbols = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe MDL2 Assets");
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, p->d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        (IUnknown **)p->dw.GetAddressOf());
    WNDCLASSW wc{};
    wc.lpfnWndProc = Impl::home_proc;
    wc.hInstance = i;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(i, MAKEINTRESOURCEW(101));
    wc.lpszClassName = L"AirConnect.Home";
    RegisterClassW(&wc);
    wc.lpfnWndProc = Impl::settings_proc;
    wc.lpszClassName = L"AirConnect.Settings";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"AirConnect.Home", L"AirConnect", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 780, 660, nullptr, nullptr, i, p.get());
    if (!w)
        return 1;
    p->theme();
    p->tray.cbSize = sizeof(p->tray);
    p->tray.hWnd = w;
    p->tray.uID = 1;
    p->tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    p->tray.uCallbackMessage = TrayMessage;
    p->tray.hIcon = LoadIconW(i, MAKEINTRESOURCEW(101));
    wcscpy_s(p->tray.szTip, L"AirConnect");
    Shell_NotifyIconW(NIM_ADD, &p->tray);
    restore_window_state(w, L"home", !background);
    if (!background)
        ShowWindow(w, IsZoomed(w) ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(w);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (p->settings && IsDialogMessageW(p->settings, &msg))
            continue;
        if (IsDialogMessageW(w, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
} // namespace ac
