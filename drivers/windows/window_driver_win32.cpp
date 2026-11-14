#include <drivers/windows/window_driver_win32.h>
#include <backends/imgui_impl_win32.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <intrin.h>
#include <algorithm>
#include <thread>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#ifndef NIIF_RESPECT_QUIET_TIME
#define NIIF_RESPECT_QUIET_TIME 0x00000080
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

namespace lumen::drivers {

static constexpr const wchar_t* WINDOW_CLASS = L"LumenWindowClass";
static constexpr const wchar_t* POPUP_CLASS = L"LumenPopupWindowClass";

std::wstring WindowDriverWin32::utf8_to_wide(std::string_view p_text)
{
    if (p_text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, p_text.data(), (int)p_text.size(), nullptr, 0);
    std::wstring out((size_t)(std::max)(n, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p_text.data(), (int)p_text.size(), out.data(), n);
    return out;
}

Error WindowDriverWin32::initialize()
{
    using enum Error;

    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = _wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS;
    LUMEN_ERR_FAIL_COND_V_MSG(!RegisterClassW(&wc), FAILED, "Couldn't register the Win32 window class.");

    WNDCLASSW popup{};
    popup.style = CS_DROPSHADOW;
    popup.lpfnWndProc = _wnd_proc;
    popup.hInstance = instance;
    popup.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popup.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    popup.lpszClassName = POPUP_CLASS;
    LUMEN_ERR_FAIL_COND_V_MSG(!RegisterClassW(&popup), FAILED, "Couldn't register the Win32 popup window class.");

    return OK;
}

void WindowDriverWin32::shutdown()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    UnregisterClassW(WINDOW_CLASS, instance);
    UnregisterClassW(POPUP_CLASS, instance);
}

void WindowDriverWin32::poll_events()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool WindowDriverWin32::wait_events(uint32_t p_timeout_ms)
{
    const DWORD r = MsgWaitForMultipleObjectsEx(0, nullptr, p_timeout_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    poll_events();
    return r == WAIT_OBJECT_0;
}

Error WindowDriverWin32::window_create(Window& r_window, const WindowCreateInfo& p_ci)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V_MSG(r_window.hwnd != nullptr, FAILED, "Window already created.");

    r_window.close_requested = false;
    r_window.frame = p_ci.frame;
    r_window.resizable = p_ci.resizable;
    r_window.imgui = p_ci.imgui;
    r_window.quit_on_destroy = p_ci.quit_on_destroy;
    r_window.on_message = p_ci.on_message;

    const bool borderless = p_ci.frame == Frame::BORDERLESS;
    DWORD style = borderless ? WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX : WS_OVERLAPPEDWINDOW;
    if (!borderless && !p_ci.resizable) style &= ~WS_THICKFRAME;
    if (!p_ci.maximizable) style &= ~WS_MAXIMIZEBOX;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (p_ci.centered) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - p_ci.width) / 2;
        y = work.top + ((work.bottom - work.top) - p_ci.height) / 2;
    }

    const std::wstring title = utf8_to_wide(p_ci.title);
    const HWND hwnd = CreateWindowExW(
        borderless ? WS_EX_APPWINDOW : 0, borderless ? POPUP_CLASS : WINDOW_CLASS, title.c_str(), style,
        x, y, p_ci.width, p_ci.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), &r_window
    );
    if (!hwnd) {
        _window_release(r_window);
        LUMEN_ERR_FAIL_COND_V_MSG(true, FAILED, "Couldn't create Win32 window.");
    }

    if (p_ci.frame != Frame::SYSTEM) {
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (p_ci.frame == Frame::CUSTOM) _window_layout_controls(r_window);

    RECT client{};
    GetClientRect(hwnd, &client);
    r_window.width = (uint32_t)(client.right - client.left);
    r_window.height = (uint32_t)(client.bottom - client.top);
    return OK;
}

void WindowDriverWin32::_window_release(Window& r_window)
{
    _window_free_owned_icons(r_window);
    r_window.hwnd = nullptr;
    r_window.width = 0;
    r_window.height = 0;
    r_window.min_width = 0;
    r_window.min_height = 0;
    r_window.frame = Frame::SYSTEM;
    r_window.resizable = true;
    r_window.imgui = false;
    r_window.quit_on_destroy = false;
    r_window.on_message = nullptr;
    r_window.titlebar_height = 0;
    r_window.titlebar_interactive_rects.clear();
    r_window.ctrl_min = r_window.ctrl_max = r_window.ctrl_close = RECT{};
    r_window.has_ctrls = false;
    r_window.focus_gained = false;
    r_window.resized = false;
    r_window.notify_icon_added = false;
}

void WindowDriverWin32::_window_layout_controls(Window& r_window)
{
    RECT c{};
    GetClientRect(r_window.hwnd, &c);
    const long bw = CAPTION_BUTTON_WIDTH;
    const long bh = CAPTION_BUTTON_HEIGHT;
    r_window.ctrl_min = { c.right - bw * 3, 0, c.right - bw * 2, bh };
    r_window.ctrl_max = { c.right - bw * 2, 0, c.right - bw, bh };
    r_window.ctrl_close = { c.right - bw, 0, c.right, bh };
    r_window.has_ctrls = true;
}

void WindowDriverWin32::window_free(Window& r_window)
{
    if (!r_window.hwnd) return;
    _notify_icon_remove(r_window);
    if (GetWindowThreadProcessId(r_window.hwnd, nullptr) == GetCurrentThreadId()) DestroyWindow(r_window.hwnd);
    else PostMessageW(r_window.hwnd, MSG_FREE, 0, 0);
}

void WindowDriverWin32::window_show(Window& r_window)
{
    if (r_window.hwnd) ShowWindow(r_window.hwnd, SW_SHOW);
}

void WindowDriverWin32::window_hide(Window& r_window)
{
    if (r_window.hwnd) ShowWindow(r_window.hwnd, SW_HIDE);
}

void WindowDriverWin32::window_focus(Window& r_window)
{
    if (!r_window.hwnd) return;
    if (IsIconic(r_window.hwnd)) ShowWindow(r_window.hwnd, SW_RESTORE);
    SetForegroundWindow(r_window.hwnd);
}

void WindowDriverWin32::window_request_close(Window& r_window)
{
    r_window.close_requested = true;
}

void WindowDriverWin32::window_cancel_close(Window& r_window)
{
    r_window.close_requested = false;
}

void WindowDriverWin32::_window_free_owned_icons(Window& r_window)
{
    if (r_window.owned_icon_big) DestroyIcon(r_window.owned_icon_big);
    if (r_window.owned_icon_small) DestroyIcon(r_window.owned_icon_small);
    r_window.owned_icon_big = nullptr;
    r_window.owned_icon_small = nullptr;
}

Error WindowDriverWin32::window_set_icon(Window& r_window, HICON p_icon)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!p_icon, FAILED);
    LUMEN_ERR_FAIL_COND_V(!r_window.hwnd, FAILED);
    SendMessageW(r_window.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(p_icon));
    SendMessageW(r_window.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(p_icon));
    _window_free_owned_icons(r_window);
    return OK;
}

Error WindowDriverWin32::window_set_icon_file(Window& r_window, const std::wstring& p_path)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!r_window.hwnd, FAILED);
    HICON big = (HICON)LoadImageW(nullptr, p_path.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    HICON mini = (HICON)LoadImageW(nullptr, p_path.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    if (!big || !mini) {
        if (big) DestroyIcon(big);
        if (mini) DestroyIcon(mini);
        return FAILED;
    }
    SendMessageW(r_window.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    SendMessageW(r_window.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(mini));
    _window_free_owned_icons(r_window);
    r_window.owned_icon_big = big;
    r_window.owned_icon_small = mini;
    return OK;
}

Error WindowDriverWin32::window_set_title(Window& r_window, std::string_view p_title)
{
    using enum Error;
    if (!r_window.hwnd) return FAILED;
    return SetWindowTextW(r_window.hwnd, utf8_to_wide(p_title).c_str()) ? OK : FAILED;
}

Error WindowDriverWin32::window_set_titlebar_color(Window& r_window, COLORREF p_color)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!r_window.hwnd, FAILED);
    HRESULT result = DwmSetWindowAttribute(r_window.hwnd, DWMWA_CAPTION_COLOR, &p_color, sizeof(p_color));
    LUMEN_ERR_FAIL_COND_V_MSG(FAILED(result), FAILED, "Failed to set Win32 window titlebar color - DWMWA_CAPTION_COLOR requires Windows 11 (build 22000+).");
    return OK;
}

void WindowDriverWin32::window_set_size(Window& r_window, int w, int h)
{
    if (!r_window.hwnd) return;
    RECT rect{ 0, 0, w, h };
    DWORD style = (DWORD)GetWindowLongPtrW(r_window.hwnd, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongPtrW(r_window.hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    SetWindowPos(r_window.hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowDriverWin32::window_set_minimum_size(Window& r_window, int w, int h)
{
    r_window.min_width = w;
    r_window.min_height = h;
}

void WindowDriverWin32::window_minimize(Window& r_window)
{
    if (r_window.hwnd) ShowWindow(r_window.hwnd, SW_MINIMIZE);
}

void WindowDriverWin32::window_toggle_maximize(Window& r_window)
{
    if (r_window.hwnd) ShowWindow(r_window.hwnd, IsZoomed(r_window.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

bool WindowDriverWin32::window_should_close(const Window& p_window) const
{
    return p_window.close_requested;
}

bool WindowDriverWin32::window_is_maximized(const Window& p_window) const
{
    return p_window.hwnd && IsZoomed(p_window.hwnd);
}

bool WindowDriverWin32::window_is_minimized(const Window& p_window) const
{
    return p_window.hwnd && IsIconic(p_window.hwnd);
}

bool WindowDriverWin32::window_is_focused(const Window& p_window) const
{
    return p_window.hwnd && GetForegroundWindow() == p_window.hwnd;
}

bool WindowDriverWin32::window_is_visible(const Window& p_window) const
{
    if (!p_window.hwnd || !IsWindowVisible(p_window.hwnd) || IsIconic(p_window.hwnd)) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(p_window.hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return p_window.width > 0 && p_window.height > 0;
}

bool WindowDriverWin32::window_has_custom_titlebar(const Window& p_window) const
{
    return p_window.frame == Frame::CUSTOM;
}

bool WindowDriverWin32::window_consume_focus_gained(Window& r_window)
{
    const bool gained = r_window.focus_gained;
    r_window.focus_gained = false;
    return gained;
}

bool WindowDriverWin32::window_consume_resized(Window& r_window)
{
    const bool resized = r_window.resized;
    r_window.resized = false;
    return resized;
}

void WindowDriverWin32::notify(Window& r_window, const std::string& p_title, const std::string& p_message)
{
    if (!r_window.hwnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = r_window.hwnd;
    nid.uID = NOTIFY_ICON_ID;

    if (!r_window.notify_icon_added) {
        HICON icon = (HICON)SendMessageW(r_window.hwnd, WM_GETICON, ICON_SMALL, 0);
        if (!icon) icon = (HICON)GetClassLongPtrW(r_window.hwnd, GCLP_HICONSM);
        if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
        nid.uFlags = NIF_ICON | NIF_TIP;
        nid.hIcon = icon;
        wcsncpy_s(nid.szTip, L"Lumen", _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) return;
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        r_window.notify_icon_added = true;
    }

    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(nid.szInfoTitle, utf8_to_wide(p_title).c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, utf8_to_wide(p_message).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void WindowDriverWin32::window_flash(Window& r_window)
{
    if (!r_window.hwnd) return;
    FLASHWINFO fi{};
    fi.cbSize = sizeof(fi);
    fi.hwnd = r_window.hwnd;
    fi.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
    FlashWindowEx(&fi);
}

void WindowDriverWin32::_notify_icon_remove(Window& r_window)
{
    if (!r_window.notify_icon_added || !r_window.hwnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = r_window.hwnd;
    nid.uID = NOTIFY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    r_window.notify_icon_added = false;
}

void WindowDriverWin32::window_set_custom_titlebar(Window& r_window, bool p_enabled)
{
    if (!r_window.hwnd || r_window.frame == Frame::BORDERLESS || (r_window.frame == Frame::CUSTOM) == p_enabled) return;
    r_window.frame = p_enabled ? Frame::CUSTOM : Frame::SYSTEM;
    DWORD corner = p_enabled ? DWMWCP_ROUND : 0;
    DwmSetWindowAttribute(r_window.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    SetWindowPos(r_window.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (p_enabled) {
        _window_layout_controls(r_window);
    } else {
        r_window.titlebar_height = 0;
        r_window.titlebar_interactive_rects.clear();
        r_window.has_ctrls = false;
    }
}

void WindowDriverWin32::window_titlebar_reset(Window& r_window, int height)
{
    r_window.titlebar_height = height;
    r_window.titlebar_interactive_rects.clear();
}

void WindowDriverWin32::window_titlebar_add_rect(Window& r_window, long l, long t, long r, long b)
{
    r_window.titlebar_interactive_rects.push_back(RECT{ l, t, r, b });
}

void WindowDriverWin32::window_titlebar_set_controls(Window& r_window, RECT min, RECT max, RECT close)
{
    r_window.ctrl_min = min;
    r_window.ctrl_max = max;
    r_window.ctrl_close = close;
    r_window.has_ctrls = true;
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
    if (p_msg == WM_NCCREATE) {
        auto* created = reinterpret_cast<Window*>(reinterpret_cast<const CREATESTRUCTW*>(p_lparam)->lpCreateParams);
        created->hwnd = p_hwnd;
        SetWindowLongPtrW(p_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    }

    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(p_hwnd, GWLP_USERDATA));
    if (!window) return DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);

    if (window->imgui && ImGui_ImplWin32_WndProcHandler(p_hwnd, p_msg, p_wparam, p_lparam))
        return true;

    if (window->on_message) {
        LRESULT result = 0;
        if (window->on_message(p_hwnd, p_msg, p_wparam, p_lparam, result)) return result;
    }

    const bool custom = window->frame == Frame::CUSTOM;
    const bool framed = window->frame != Frame::SYSTEM;

    switch (p_msg) {
        case WM_NCCALCSIZE: {
            if (!p_wparam || !custom) break;
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

        case WM_NCACTIVATE: {
            if (custom) return DefWindowProcW(p_hwnd, p_msg, p_wparam, -1);
            break;
        }

        case WM_NCHITTEST: {
            if (!framed) break;
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

            if (window->imgui && ImGui::GetCurrentContext() && ImGui::IsAnyItemHovered()) return HTCLIENT;

            bool over_widget = false;
            if (cursor.y < window->titlebar_height) {
                for (const RECT& r : window->titlebar_interactive_rects) {
                    if (in(r)) {
                        over_widget = true;
                        break;
                    }
                }
            }

            const int border = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            if (window->resizable && !IsZoomed(p_hwnd)) {
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

        case WM_GETTITLEBARINFOEX: {
            if (!framed || !window->has_ctrls) break;
            TITLEBARINFOEX* info = reinterpret_cast<TITLEBARINFOEX*>(p_lparam);
            DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);
            auto to_screen = [&](RECT r) {
                MapWindowPoints(p_hwnd, HWND_DESKTOP, reinterpret_cast<POINT*>(&r), 2);
                return r;
            };
            RECT client;
            GetClientRect(p_hwnd, &client);
            info->rcTitleBar = to_screen(RECT{ 0, 0, client.right, window->titlebar_height });
            info->rgrect[2] = to_screen(window->ctrl_min);
            info->rgrect[3] = to_screen(window->ctrl_max);
            info->rgrect[5] = to_screen(window->ctrl_close);
            info->rgstate[2] = info->rgstate[3] = info->rgstate[5] = 0;
            return 0;
        }

        case WM_GETMINMAXINFO: {
            if (window->min_width > 0 || window->min_height > 0) {
                RECT r{ 0, 0, window->min_width, window->min_height };
                if (!framed) AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(p_hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(p_hwnd, GWL_EXSTYLE));
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(p_lparam);
                mmi->ptMinTrackSize.x = r.right - r.left;
                mmi->ptMinTrackSize.y = r.bottom - r.top;
                return 0;
            }
            break;
        }

        case WM_SETFOCUS: {
            window->focus_gained = true;
            break;
        }

        case WM_ACTIVATE: {
            if (LOWORD(p_wparam) != WA_INACTIVE) window->focus_gained = true;
            break;
        }

        case WM_CLOSE: {
            window->close_requested = true;
            return 0;
        }

        case MSG_FREE: {
            DestroyWindow(p_hwnd);
            return 0;
        }

        case WM_DESTROY: {
            if (window->quit_on_destroy) PostQuitMessage(0);
            return 0;
        }

        case WM_NCDESTROY: {
            SetWindowLongPtrW(p_hwnd, GWLP_USERDATA, 0);
            _window_release(*window);
            break;
        }

        case WM_SIZE: {
            window->width = LOWORD(p_lparam);
            window->height = HIWORD(p_lparam);
            window->resized = true;
            if (custom && window->has_ctrls) _window_layout_controls(*window);
            return 0;
        }

        case WM_NCLBUTTONUP: {
            if (framed) {
                if (p_wparam == HTCLOSE) { window->close_requested = true; return 0; }
                if (p_wparam == HTMINBUTTON) { ShowWindow(p_hwnd, SW_MINIMIZE); return 0; }
                if (p_wparam == HTMAXBUTTON) { ShowWindow(p_hwnd, IsZoomed(p_hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
            }
            break;
        }

        case WM_NCLBUTTONDOWN: {
            if (framed && (p_wparam == HTCLOSE || p_wparam == HTMAXBUTTON || p_wparam == HTMINBUTTON))
                return 0;
            break;
        }

        case WM_SYSCOMMAND: {
            if ((p_wparam & 0xFFF0) == SC_KEYMENU && p_lparam == 0) return 0;
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
