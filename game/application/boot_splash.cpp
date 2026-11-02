#include <game/application/boot_splash.h>
#include <windowsx.h>
#include <algorithm>
#include <thread>

namespace lumen {

Error BootSplash::show(drivers::WindowDriverWin32& p_driver, std::string_view p_title, const uint32_t* p_bgra, int p_width, int p_height)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(visible(), FAILED);

    driver = &p_driver;
    title = p_title;
    image_width = p_bgra ? p_width : 0;
    image_height = p_bgra ? p_height : 0;
    if (p_bgra) pixels.assign(p_bgra, p_bgra + (size_t)p_width * p_height);
    bar = 0.0f;
    dots = 0;
    hovered = Button::NONE;
    pressed = Button::NONE;
    tracking = false;

    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    LUMEN_ERR_FAIL_COND_V(!ready || !done, FAILED);

    std::thread([this] { _run(); }).detach();
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    ready = nullptr;
    return visible() ? OK : FAILED;
}

void BootSplash::set_status(std::string_view p_status)
{
    {
        std::lock_guard lock(mutex);
        status = drivers::WindowDriverWin32::utf8_to_wide(p_status);
    }
    if (visible()) InvalidateRect(window.hwnd, nullptr, FALSE);
}

void BootSplash::set_progress(float p_from, float p_to)
{
    progress_from = p_from;
    progress_to = p_to;
}

void BootSplash::close()
{
    if (done) {
        if (visible()) driver->window_free(window);
        WaitForSingleObject(done, CLOSE_TIMEOUT_MS);
        CloseHandle(done);
        done = nullptr;
    }
    pixels.clear();
    pixels.shrink_to_fit();
}

void BootSplash::_run()
{
    scale = (float)GetDpiForSystem() / 96.0f;
    width = _px(WIDTH);
    height = _px(HEIGHT);
    status_font = CreateFontW(-_px(STATUS_FONT_SIZE), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, STATUS_FONT);

    drivers::WindowDriverWin32::WindowCreateInfo ci;
    ci.title = title;
    ci.width = width;
    ci.height = height;
    ci.frame = drivers::WindowDriverWin32::Frame::BORDERLESS;
    ci.resizable = false;
    ci.maximizable = false;
    ci.centered = true;
    ci.quit_on_destroy = true;
    ci.on_message = [this](HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam, LRESULT& r_result) {
        return _on_message(p_hwnd, p_msg, p_wparam, p_lparam, r_result);
    };

    const bool created = driver->window_create(window, ci) == Error::OK;
    if (created) {
        driver->window_titlebar_reset(window, height);
        for (Button b : { Button::MINIMIZE, Button::CLOSE }) {
            const RECT r = _button_rect(b);
            driver->window_titlebar_add_rect(window, r.left, r.top, r.right, r.bottom);
        }
        SetTimer(window.hwnd, TIMER_DOTS, DOTS_INTERVAL_MS, nullptr);
        SetTimer(window.hwnd, TIMER_BAR, BAR_INTERVAL_MS, nullptr);
        ShowWindow(window.hwnd, SW_SHOWNORMAL);
        UpdateWindow(window.hwnd);
    }
    SetEvent(ready);

    if (created) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DeleteObject(status_font);
    if (background) DeleteObject(background);
    status_font = nullptr;
    background = nullptr;
    SetEvent(done);
}

bool BootSplash::_step_bar()
{
    const float from = progress_from;
    const float to = progress_to;
    const float prev = bar;
    if (progress_restart.exchange(false)) bar = 0.0f;
    if (bar < from) bar += (from - bar) * BAR_CATCH_UP + 0.0005f;
    else bar += (from + (to - from) * BAR_CREEP_LIMIT - bar) * BAR_CREEP;
    bar = std::clamp(bar, 0.0f, 1.0f);
    return bar != prev;
}

RECT BootSplash::_button_rect(Button p_button) const
{
    const int bw = _px(BUTTON_WIDTH);
    const int bh = _px(BUTTON_HEIGHT);
    const int right = p_button == Button::CLOSE ? width : width - bw;
    return { right - bw, 0, right, bh };
}

BootSplash::Button BootSplash::_button_at(POINT p_client) const
{
    for (Button b : { Button::MINIMIZE, Button::CLOSE }) {
        const RECT r = _button_rect(b);
        if (PtInRect(&r, p_client)) return b;
    }
    return Button::NONE;
}

void BootSplash::_draw_text(HDC p_dc, const wchar_t* p_text, int p_len, RECT p_rect, UINT p_format)
{
    const int shadow = _px(1);
    RECT s = { p_rect.left + shadow, p_rect.top + shadow, p_rect.right + shadow, p_rect.bottom + shadow };
    SetTextColor(p_dc, COLOR_TEXT_SHADOW);
    DrawTextW(p_dc, p_text, p_len, &s, p_format);
    SetTextColor(p_dc, COLOR_TEXT);
    DrawTextW(p_dc, p_text, p_len, &p_rect, p_format);
}

void BootSplash::_draw_button(HDC p_dc, Button p_button)
{
    const RECT r = _button_rect(p_button);
    if (hovered == p_button || pressed == p_button) {
        HBRUSH brush = CreateSolidBrush(p_button == Button::CLOSE ? COLOR_CLOSE_HOVER : COLOR_BUTTON_HOVER);
        FillRect(p_dc, &r, brush);
        DeleteObject(brush);
    }

    const int g = _px(GLYPH_SIZE) / 2;
    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, _px(1)), COLOR_TEXT);
    HGDIOBJ old = SelectObject(p_dc, pen);
    if (p_button == Button::MINIMIZE) {
        MoveToEx(p_dc, cx - g, cy, nullptr);
        LineTo(p_dc, cx + g + 1, cy);
    } else {
        MoveToEx(p_dc, cx - g, cy - g, nullptr);
        LineTo(p_dc, cx + g + 1, cy + g + 1);
        MoveToEx(p_dc, cx + g, cy - g, nullptr);
        LineTo(p_dc, cx - g - 1, cy + g + 1);
    }
    SelectObject(p_dc, old);
    DeleteObject(pen);
}

void BootSplash::_draw_background(HDC p_dc)
{
    HDC mem = CreateCompatibleDC(p_dc);
    if (!background) {
        background = CreateCompatibleBitmap(p_dc, width, height);
        HGDIOBJ old = SelectObject(mem, background);
        const RECT client = { 0, 0, width, height };
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(mem, &client, brush);
        DeleteObject(brush);
        if (!pixels.empty()) {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = image_width;
            bi.bmiHeader.biHeight = -image_height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(mem, HALFTONE);
            StretchDIBits(mem, 0, 0, width, height, 0, 0, image_width, image_height, pixels.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        SelectObject(mem, old);
    }
    HGDIOBJ old = SelectObject(mem, background);
    BitBlt(p_dc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void BootSplash::_paint(HDC p_dc)
{
    _draw_background(p_dc);

    _draw_button(p_dc, Button::MINIMIZE);
    _draw_button(p_dc, Button::CLOSE);

    const int bar_h = (std::max)(1, _px(BAR_HEIGHT));
    const RECT track = { 0, height - bar_h, width, height };
    const RECT fill = { 0, track.top, (int)(width * bar + 0.5f), height };
    HBRUSH track_brush = CreateSolidBrush(COLOR_BAR_TRACK);
    HBRUSH fill_brush = CreateSolidBrush(COLOR_BAR_FILL);
    FillRect(p_dc, &track, track_brush);
    FillRect(p_dc, &fill, fill_brush);
    DeleteObject(track_brush);
    DeleteObject(fill_brush);

    std::wstring text;
    {
        std::lock_guard lock(mutex);
        text = status;
    }
    if (text.empty()) return;

    SelectObject(p_dc, status_font);
    SIZE dots_size{};
    GetTextExtentPoint32W(p_dc, L"...", 3, &dots_size);
    const int m = _px(STATUS_MARGIN);
    const RECT dots_rect = { width - m - dots_size.cx, 0, width - m, height - m - bar_h };
    const RECT text_rect = { 0, 0, dots_rect.left, height - m - bar_h };
    _draw_text(p_dc, text.c_str(), (int)text.size(), text_rect, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    _draw_text(p_dc, L"...", (int)dots, dots_rect, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
}

bool BootSplash::_on_message(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam, LRESULT& r_result)
{
    r_result = 0;
    switch (p_msg) {
        case WM_MOUSEMOVE: {
            if (!tracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, p_hwnd, 0 };
                tracking = TrackMouseEvent(&tme) != FALSE;
            }
            const Button b = _button_at({ GET_X_LPARAM(p_lparam), GET_Y_LPARAM(p_lparam) });
            if (b != hovered) {
                hovered = b;
                InvalidateRect(p_hwnd, nullptr, FALSE);
            }
            return true;
        }
        case WM_MOUSELEAVE:
            tracking = false;
            hovered = Button::NONE;
            InvalidateRect(p_hwnd, nullptr, FALSE);
            return true;
        case WM_LBUTTONDOWN:
            pressed = _button_at({ GET_X_LPARAM(p_lparam), GET_Y_LPARAM(p_lparam) });
            if (pressed != Button::NONE) SetCapture(p_hwnd);
            InvalidateRect(p_hwnd, nullptr, FALSE);
            return true;
        case WM_LBUTTONUP: {
            const Button b = _button_at({ GET_X_LPARAM(p_lparam), GET_Y_LPARAM(p_lparam) });
            const Button was = pressed;
            pressed = Button::NONE;
            ReleaseCapture();
            InvalidateRect(p_hwnd, nullptr, FALSE);
            if (b == was && b == Button::MINIMIZE) ShowWindow(p_hwnd, SW_MINIMIZE);
            if (b == was && b == Button::CLOSE) SendMessageW(p_hwnd, WM_CLOSE, 0, 0);
            return true;
        }
        case WM_CLOSE:
            ShowWindow(p_hwnd, SW_HIDE);
            return false;
        case WM_TIMER:
            if (p_wparam == TIMER_DOTS) {
                dots = (dots + 1) % 4;
                InvalidateRect(p_hwnd, nullptr, FALSE);
            } else if (p_wparam == TIMER_BAR && _step_bar()) {
                InvalidateRect(p_hwnd, nullptr, FALSE);
            }
            return true;
        case WM_ERASEBKGND:
            r_result = 1;
            return true;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(p_hwnd, &ps);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, width, height);
            HGDIOBJ old_bmp = SelectObject(mem, bmp);
            HGDIOBJ old_font = SelectObject(mem, status_font);
            SetBkMode(mem, TRANSPARENT);
            _paint(mem);
            BitBlt(dc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old_font);
            SelectObject(mem, old_bmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(p_hwnd, &ps);
            return true;
        }
        case WM_DESTROY:
            KillTimer(p_hwnd, TIMER_DOTS);
            KillTimer(p_hwnd, TIMER_BAR);
            return false;
    }
    return false;
}

}
