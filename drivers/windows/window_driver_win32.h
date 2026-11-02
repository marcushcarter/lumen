#pragma once
#include <core/base/error.h>
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lumen::drivers {

struct WindowDriverWin32
{
    /***************/
    /**** SETUP ****/
    /***************/

    Error initialize();
    void shutdown();
    void poll_events();
    bool wait_events(uint32_t p_timeout_ms);

    static std::wstring utf8_to_wide(std::string_view p_text);

    /****************/
    /**** WINDOW ****/
    /****************/

    static constexpr UINT MSG_FREE = WM_APP + 1;
    static constexpr long CAPTION_BUTTON_WIDTH = 46;
    static constexpr long CAPTION_BUTTON_HEIGHT = 34;

    enum class Frame : uint8_t { SYSTEM, CUSTOM, BORDERLESS };

    struct WindowCreateInfo {
        std::string title;
        int width = 1280;
        int height = 720;
        Frame frame = Frame::SYSTEM;
        bool resizable = true;
        bool maximizable = true;
        bool centered = false;
        bool imgui = false;
        bool quit_on_destroy = false;
        std::function<bool(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam, LRESULT& r_result)> on_message;
    };

    struct Window {
        HWND hwnd = nullptr;
        std::atomic<bool> close_requested = false;
        uint32_t width = 0;
        uint32_t height = 0;
        int min_width = 0;
        int min_height = 0;

        Frame frame = Frame::SYSTEM;
        bool resizable = true;
        bool imgui = false;
        bool quit_on_destroy = false;
        std::function<bool(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam, LRESULT& r_result)> on_message;

        int titlebar_height = 0;
        std::vector<RECT> titlebar_interactive_rects;

        RECT ctrl_min{}, ctrl_max{}, ctrl_close{};
        bool has_ctrls = false;

        bool focus_gained = false;
        bool resized = false;
        bool notify_icon_added = false;

        HICON owned_icon_big = nullptr;
        HICON owned_icon_small = nullptr;
    };

    static LRESULT CALLBACK _wnd_proc(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam);
    static void _window_release(Window& r_window);
    static void _window_free_owned_icons(Window& r_window);
    static void _window_layout_controls(Window& r_window);

    Error window_create(Window& r_window, const WindowCreateInfo& p_ci);
    void window_free(Window& r_window);
    void window_show(Window& r_window);
    void window_hide(Window& r_window);
    void window_focus(Window& r_window);
    void window_request_close(Window& r_window);
    void window_cancel_close(Window& r_window);

    Error window_set_icon(Window& r_window, HICON p_icon);
    Error window_set_icon_file(Window& r_window, const std::wstring& p_path);
    Error window_set_title(Window& r_window, std::string_view p_title);
    Error window_set_titlebar_color(Window& r_window, COLORREF p_color);
    void window_set_size(Window& r_window, int w, int h);
    void window_set_minimum_size(Window& r_window, int w, int h);
    void window_minimize(Window& r_window);
    void window_toggle_maximize(Window& r_window);

    bool window_should_close(const Window& p_window) const;
    bool window_is_maximized(const Window& p_window) const;
    bool window_is_minimized(const Window& p_window) const;
    bool window_is_focused(const Window& p_window) const;
    bool window_is_visible(const Window& p_window) const;
    bool window_has_custom_titlebar(const Window& p_window) const;
    bool window_consume_focus_gained(Window& r_window);
    bool window_consume_resized(Window& r_window);

    void window_set_custom_titlebar(Window& r_window, bool p_enabled);
    void window_titlebar_reset(Window& r_window, int height);
    void window_titlebar_add_rect(Window& r_window, long left, long top, long right, long bottom);
    void window_titlebar_set_controls(Window& r_window, RECT min, RECT max, RECT close);

    /****************/
    /**** NOTIFY ****/
    /****************/

    static constexpr UINT NOTIFY_ICON_ID = 1;

    void _notify_icon_remove(Window& r_window);
    void notify(Window& r_window, const std::string& p_title, const std::string& p_message);
    void window_flash(Window& r_window);

    /****************/
    /**** SYSTEM ****/
    /****************/

    struct SystemInfo {
        std::string os_name;
        uint32_t os_build = 0;
        std::string cpu_brand;
        uint32_t cpu_cores = 0;
        uint32_t cpu_threads = 0;
        uint32_t cpu_mhz = 0;
        uint64_t ram_total_bytes = 0;
        int monitor_count = 0;
        std::string locale;
    };

    static SystemInfo get_system_info();
    static bool system_accent_color(float& r_r, float& r_g, float& r_b);
};

}
