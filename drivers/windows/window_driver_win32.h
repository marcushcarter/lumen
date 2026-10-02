#pragma once
#include <core/base/error.h>
#include <windows.h>
#include <functional>
#include <string>
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

    /****************/
    /**** WINDOW ****/
    /****************/

    struct Window {
        HWND hwnd = nullptr;
        bool close_requested = false;
        uint32_t width = 0;
        uint32_t height = 0;

        bool custom_titlebar = false;
        int  titlebar_height = 0;
        std::vector<RECT> titlebar_interactive_rects;

        RECT ctrl_min{}, ctrl_max{}, ctrl_close{};
        bool has_ctrls = false;
    };

    Window window;

    Error window_create(const std::string& p_title, int p_width, int p_height, bool p_custom_titlebar = false);
    void window_bind();
    void window_free();

    void window_show();
    void window_hide();

    bool window_should_close();
    void window_request_close();

    Error window_set_icon(HICON p_icon);
    Error window_set_title(std::string_view p_title);
    Error window_set_titlebar_color(COLORREF p_color);

    void window_set_size(int w, int h);
    void window_minimize();
    void window_toggle_maximize();
    bool window_is_maximized();
    bool window_is_minimized();
    
    void window_set_custom_titlebar(bool p_enabled);
    void window_titlebar_reset(int height);
    void window_titlebar_add_rect(long left, long top, long right, long bottom);
    void window_titlebar_set_controls(RECT min, RECT max, RECT close);

    static bool system_accent_color(float& r_r, float& r_g, float& r_b);

    static LRESULT CALLBACK _wnd_proc(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam);
    
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
};

}