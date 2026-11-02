#pragma once
#include <drivers/windows/window_driver_win32.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

struct BootSplash
{
    static constexpr int WIDTH = 960;
    static constexpr int HEIGHT = 540;
    static constexpr int BUTTON_WIDTH = 46;
    static constexpr int BUTTON_HEIGHT = 32;
    static constexpr int GLYPH_SIZE = 10;
    static constexpr int STATUS_MARGIN = 24;
    static constexpr int STATUS_FONT_SIZE = 16;
    static constexpr const wchar_t* STATUS_FONT = L"Segoe UI";
    static constexpr int BAR_HEIGHT = 3;
    static constexpr COLORREF COLOR_TEXT = RGB(240, 240, 240);
    static constexpr COLORREF COLOR_TEXT_SHADOW = RGB(0, 0, 0);
    static constexpr COLORREF COLOR_BAR_TRACK = RGB(48, 48, 52);
    static constexpr COLORREF COLOR_BAR_FILL = RGB(232, 232, 232);
    static constexpr COLORREF COLOR_BUTTON_HOVER = RGB(64, 64, 68);
    static constexpr COLORREF COLOR_CLOSE_HOVER = RGB(196, 43, 28);
    static constexpr UINT TIMER_DOTS = 1;
    static constexpr UINT TIMER_BAR = 2;
    static constexpr UINT DOTS_INTERVAL_MS = 400;
    static constexpr UINT BAR_INTERVAL_MS = 16;
    static constexpr float BAR_CATCH_UP = 0.2f;
    static constexpr float BAR_CREEP = 0.01f;
    static constexpr float BAR_CREEP_LIMIT = 0.9f;
    static constexpr DWORD CLOSE_TIMEOUT_MS = 2000;

    enum class Button : uint8_t { NONE, MINIMIZE, CLOSE };

    drivers::WindowDriverWin32* driver = nullptr;
    drivers::WindowDriverWin32::Window window;
    std::string title;
    std::vector<uint32_t> pixels;
    int image_width = 0;
    int image_height = 0;
    float scale = 1.0f;
    int width = 0;
    int height = 0;

    HANDLE ready = nullptr;
    HANDLE done = nullptr;
    HFONT status_font = nullptr;
    HBITMAP background = nullptr;
    std::mutex mutex;
    std::wstring status;
    std::atomic<float> progress_from{ 0.0f };
    std::atomic<float> progress_to{ 0.0f };
    std::atomic<bool> progress_restart{ false };
    float bar = 0.0f;
    Button hovered = Button::NONE;
    Button pressed = Button::NONE;
    uint32_t dots = 0;
    bool tracking = false;

    Error show(drivers::WindowDriverWin32& p_driver, std::string_view p_title, const uint32_t* p_bgra, int p_width, int p_height);
    void set_status(std::string_view p_status);
    void set_progress(float p_from, float p_to);
    void restart_progress() { progress_restart = true; }
    void close();
    bool visible() const { return window.hwnd != nullptr; }
    bool close_requested() const { return visible() && driver->window_should_close(window); }

    void _run();
    bool _on_message(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam, LRESULT& r_result);
    void _paint(HDC p_dc);
    void _draw_background(HDC p_dc);
    bool _step_bar();
    void _draw_button(HDC p_dc, Button p_button);
    void _draw_text(HDC p_dc, const wchar_t* p_text, int p_len, RECT p_rect, UINT p_format);
    RECT _button_rect(Button p_button) const;
    Button _button_at(POINT p_client) const;
    int _px(int p_value) const { return (int)(p_value * scale + 0.5f); }
};

}
