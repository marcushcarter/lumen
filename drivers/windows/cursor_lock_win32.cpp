#include <drivers/windows/cursor_lock_win32.h>
#include <windows.h>

namespace lumen::drivers {

void CursorLockWin32::begin()
{
    POINT p{};
    GetCursorPos(&p);
    anchor_x = p.x;
    anchor_y = p.y;
    active = true;
}

void CursorLockWin32::end(float p_offset_x, float p_offset_y)
{
    if (!active) return;
    SetCursorPos((int)(anchor_x + (long)p_offset_x), (int)(anchor_y + (long)p_offset_y));
    active = false;
}

void CursorLockWin32::take_delta(float& r_dx, float& r_dy)
{
    r_dx = 0.0f;
    r_dy = 0.0f;
    if (!active) return;
    POINT p{};
    GetCursorPos(&p);
    r_dx = (float)(p.x - anchor_x);
    r_dy = (float)(p.y - anchor_y);
    SetCursorPos((int)anchor_x, (int)anchor_y);
}

}