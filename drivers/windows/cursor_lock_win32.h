#pragma once

namespace lumen::drivers {

struct CursorLockWin32
{
    long anchor_x = 0;
    long anchor_y = 0;
    bool active = false;

    void begin();
    void end(float p_offset_x = 0.0f, float p_offset_y = 0.0f);
    void take_delta(float& r_dx, float& r_dy);
};

}