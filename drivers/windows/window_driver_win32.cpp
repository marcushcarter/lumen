#include <drivers/windows/window_driver_win32.h>
#include <backends/imgui_impl_win32.h>
#include <dwmapi.h>
#include <windowsx.h>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

namespace lumen::drivers {
    
static const wchar_t* LUMEN_WINDOW_CLASS = L"LumenWindowClass";

static std::wstring utf8_to_wstring(const std::string& str)
{
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), size);
    return result;
}

Error WindowDriverWin32::initialize()
{
    using enum Error;

    WNDCLASSW wc{};
    wc.lpfnWndProc = _wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = LUMEN_WINDOW_CLASS;
    RegisterClassW(&wc);

    return Ok;
}

void WindowDriverWin32::shutdown()
{
    UnregisterClassW(LUMEN_WINDOW_CLASS, GetModuleHandleW(nullptr));
}

void WindowDriverWin32::poll_events()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

Error WindowDriverWin32::window_create(const std::string& p_title, int p_width, int p_height, bool p_custom_titlebar)
{
    using enum Error;

    window.custom_titlebar = p_custom_titlebar;
    
    std::wstring title = utf8_to_wstring(p_title);

    window.hwnd = CreateWindowExW(
        0, LUMEN_WINDOW_CLASS, title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, p_width, p_height,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    LUMEN_ERR_FAIL_COND_V_MSG(!window.hwnd, Failed, "Couldn't create Win32 window.");

    window.width = static_cast<uint32_t>(p_width);
    window.height = static_cast<uint32_t>(p_height);

    return Ok;
}

void WindowDriverWin32::window_bind()
{
    if (!window.hwnd) return;
    SetWindowLongPtrW(window.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&window));

    if (window.custom_titlebar) {
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        SetWindowPos(window.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        RECT c;
        GetClientRect(window.hwnd, &c);
        const long BW = 46, MH = 34;
        window.ctrl_min = { c.right - BW*3, 0, c.right - BW*2, MH };
        window.ctrl_max = { c.right - BW*2, 0, c.right - BW*1, MH };
        window.ctrl_close = { c.right - BW*1, 0, c.right, MH };
        window.has_ctrls = true;
    }

    // ShowWindow(window.hwnd, SW_SHOW);
}

void WindowDriverWin32::window_free()
{
    if (window.hwnd) {
        DestroyWindow(window.hwnd);
        window.hwnd = nullptr;
    }
}

void WindowDriverWin32::window_show()
{
    if (!window.hwnd) return;
    ShowWindow(window.hwnd, SW_SHOW);
}

void WindowDriverWin32::window_hide()
{
    if (!window.hwnd) return;
    ShowWindow(window.hwnd, SW_HIDE);
}

bool WindowDriverWin32::window_should_close()
{
    return window.close_requested;
}

void WindowDriverWin32::window_request_close()
{
    window.close_requested = true;
}

Error WindowDriverWin32::window_set_icon(HICON p_icon)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!p_icon, Failed);
    LUMEN_ERR_FAIL_COND_V(!window.hwnd, Failed);
    SendMessageW(window.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(p_icon));
    SendMessageW(window.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(p_icon));
    return Ok;
}

Error WindowDriverWin32::window_set_title(std::string_view p_title)
{
    using enum Error;
    if (!window.hwnd) return Failed;
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, p_title.data(), (int)p_title.size(), nullptr, 0);
    if (wide_len <= 0) return Failed;
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p_title.data(), (int)p_title.size(), wide.data(), wide_len);
    return SetWindowTextW(window.hwnd, wide.c_str()) ? Ok : Failed;
}

Error WindowDriverWin32::window_set_titlebar_color(COLORREF p_color)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!window.hwnd, Failed);
    HRESULT result = DwmSetWindowAttribute(window.hwnd, DWMWA_CAPTION_COLOR, &p_color, sizeof(p_color));
    LUMEN_ERR_FAIL_COND_V_MSG(FAILED(result), Failed, "Failed to set Win32 window titlebar color - DWMWA_CAPTION_COLOR requires Windows 11 (build 22000+).");
    return Ok;
}

void WindowDriverWin32::window_set_size(int w, int h)
{
    RECT rect{ 0, 0, w, h };
    DWORD style = (DWORD)GetWindowLongPtrW(window.hwnd, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongPtrW(window.hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    SetWindowPos(window.hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowDriverWin32::window_minimize()
{
    if (window.hwnd) ShowWindow(window.hwnd, SW_MINIMIZE);
}

void WindowDriverWin32::window_toggle_maximize()
{
    if (!window.hwnd) return;
    ShowWindow(window.hwnd, IsZoomed(window.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

bool WindowDriverWin32::window_is_maximized()
{
    return window.hwnd && IsZoomed(window.hwnd);
}

bool WindowDriverWin32::window_is_minimized()
{
    return window.hwnd && IsIconic(window.hwnd);
}

void WindowDriverWin32::window_set_custom_titlebar(bool p_enabled)
{
    if (!window.hwnd || window.custom_titlebar == p_enabled) return;
    window.custom_titlebar = p_enabled;
    DWORD corner = p_enabled ? DWMWCP_ROUND : 0;
    DwmSetWindowAttribute(window.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    SetWindowPos(window.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (p_enabled) {
        RECT c;
        GetClientRect(window.hwnd, &c);
        const long BW = 46, MH = 34;
        window.ctrl_min = { c.right - BW*3, 0, c.right - BW*2, MH };
        window.ctrl_max = { c.right - BW*2, 0, c.right - BW*1, MH };
        window.ctrl_close = { c.right - BW*1, 0, c.right, MH };
        window.has_ctrls = true;
    } else {
        window.titlebar_height = 0;
        window.titlebar_interactive_rects.clear();
        window.has_ctrls = false;
    }
}

void WindowDriverWin32::window_titlebar_reset(int height)
{
    window.titlebar_height = height;
    window.titlebar_interactive_rects.clear();
}

void WindowDriverWin32::window_titlebar_add_rect(long l, long t, long r, long b)
{
    window.titlebar_interactive_rects.push_back(RECT{ l, t, r, b });
}

void WindowDriverWin32::window_titlebar_set_controls(RECT min, RECT max, RECT close)
{
    window.ctrl_min = min;
    window.ctrl_max = max;
    window.ctrl_close = close;
    window.has_ctrls = true;
}

bool WindowDriverWin32::system_accent_color(float& r_r, float& r_g, float& r_b)
{
    DWORD argb = 0; BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&argb, &opaque))) return false;
    r_r = ((argb >> 16) & 0xFF) / 255.0f;
    r_g = ((argb >>  8) & 0xFF) / 255.0f;
    r_b = ( argb        & 0xFF) / 255.0f;
    return true;
}

LRESULT CALLBACK WindowDriverWin32::_wnd_proc(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(p_hwnd, p_msg, p_wparam, p_lparam))
        return true;

    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(p_hwnd, GWLP_USERDATA));

    switch (p_msg) {
        case WM_NCCALCSIZE: {
            if (!p_wparam || !window || !window->custom_titlebar) break;
            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(p_lparam);
            RECT& rect = params->rgrc[0];
            if (IsZoomed(p_hwnd)) {
                int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                rect.left += fx;
                rect.right -= fx;
                rect.top += fy;
                rect.bottom -= fy;
            }
            return 0;
        }

        case WM_NCHITTEST: {
            if (!window || !window->custom_titlebar) break;
            POINT cursor = { GET_X_LPARAM(p_lparam), GET_Y_LPARAM(p_lparam) };
            RECT client;
            ScreenToClient(p_hwnd, &cursor);
            GetClientRect(p_hwnd, &client);

            auto in = [&](const RECT& r) {
                return cursor.x >= r.left && cursor.x < r.right && cursor.y >= r.top  && cursor.y < r.bottom;
            };

            if (window->has_ctrls) {
                if (in(window->ctrl_close)) return HTCLOSE;
                if (in(window->ctrl_max))   return HTMAXBUTTON;
                if (in(window->ctrl_min))   return HTMINBUTTON;
            }

            if (ImGui::IsAnyItemHovered()) return HTCLIENT;

            bool over_widget = false;
            if (cursor.y < window->titlebar_height) {
                for (const RECT& r : window->titlebar_interactive_rects) {
                    if (cursor.x >= r.left && cursor.x < r.right && cursor.y >= r.top  && cursor.y < r.bottom) {
                        over_widget = true;
                        break;
                    }
                }
            }
                
            const int border = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            if (!IsZoomed(p_hwnd)) {
                bool l = cursor.x < border;
                bool r = cursor.x >= client.right - border;
                bool t = cursor.y < border;
                bool b = cursor.y >= client.bottom - border;
                if (t && l) return HTTOPLEFT;
                if (t && r) return HTTOPRIGHT;
                if (b && l) return HTBOTTOMLEFT;
                if (b && r) return HTBOTTOMRIGHT;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                if (b) return HTBOTTOM;
                if (t && !over_widget) return HTTOP;
            }
            if (over_widget) return HTCLIENT;
            if (cursor.y < window->titlebar_height) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_CLOSE: {
            if (window) window->close_requested = true;
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }

        case WM_SIZE: {
            if (window) {
                window->width = LOWORD(p_lparam);
                window->height = HIWORD(p_lparam);
                if (window->custom_titlebar && window->has_ctrls) {
                    const long BW = 46, MH = 34;
                    long right = (long)LOWORD(p_lparam);
                    window->ctrl_min = { right - BW*3, 0, right - BW*2, MH };
                    window->ctrl_max = { right - BW*2, 0, right - BW*1, MH };
                    window->ctrl_close = { right - BW*1, 0, right, MH };
                }
            }
            return 0;
        }

        case WM_NCLBUTTONUP: {
            if (window && window->custom_titlebar) {
                if (p_wparam == HTCLOSE) { window->close_requested = true; return 0; }
                if (p_wparam == HTMINBUTTON) { ShowWindow(p_hwnd, SW_MINIMIZE); return 0; }
                if (p_wparam == HTMAXBUTTON) { ShowWindow(p_hwnd, IsZoomed(p_hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
            }
            break;
        }
        
        case WM_NCLBUTTONDOWN: {
            if (window && window->custom_titlebar && (p_wparam == HTCLOSE || p_wparam == HTMAXBUTTON || p_wparam == HTMINBUTTON))
                return 0;
            break;
        }
    }

    return DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);
}

WindowDriverWin32::SystemInfo WindowDriverWin32::get_system_info()
{
    SystemInfo info;
    
    {
        typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW rovi{};
        rovi.dwOSVersionInfoSize = sizeof(rovi);

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto fn = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (fn) fn(&rovi);
        }

        info.os_build = rovi.dwBuildNumber;

        if (rovi.dwMajorVersion == 10 && rovi.dwBuildNumber >= 22000) {
            info.os_name = "Windows 11";
        } else if (rovi.dwMajorVersion == 10) {
            info.os_name = "Windows 10";
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Windows %lu.%lu", rovi.dwMajorVersion, rovi.dwMinorVersion);
            info.os_name = buf;
        }
    }
    
    {
        int regs[4] = {};
        __cpuid(regs, 0x80000000);
        if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
            char brand[49] = {};
            __cpuid(reinterpret_cast<int*>(brand + 0),  0x80000002);
            __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
            __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
            const char* start = brand;
            while (*start == ' ') ++start;
            info.cpu_brand = start;
        } else {
            info.cpu_brand = "Unknown CPU";
        }
    }
    
    info.cpu_threads = std::thread::hardware_concurrency();

    {
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
        if (len > 0) {
            std::vector<uint8_t> buf(len);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len)) {
                uint8_t* ptr = buf.data();
                uint8_t* end = ptr + len;
                while (ptr < end) {
                    auto* rec = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
                    if (rec->Relationship == RelationProcessorCore) ++info.cpu_cores;
                    ptr += rec->Size;
                }
            }
        }
    }

    {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &size);
        info.cpu_mhz = mhz;
    }

    {
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        info.ram_total_bytes = mem.ullTotalPhys;
    }

    info.monitor_count = GetSystemMetrics(SM_CMONITORS);
    return info;
}

}