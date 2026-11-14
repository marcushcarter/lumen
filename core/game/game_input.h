#pragma once
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct GameInput
{
    static constexpr float MOUSE_SENSITIVITY = 0.12f;
    static constexpr float STICK_LOOK_RATE = 180.0f;
    static constexpr float STICK_DEADZONE = 0.15f;

    bool captured = false;
    vec2 move = vec2(0.0f);
    vec2 look = vec2(0.0f);
    bool sprint = false;
    bool aim = false;
    bool jump = false;
    bool crouch_toggle = false;
    bool walk_toggle = false;
    bool rotation_toggle = false;

    void clear();
    void read(bool p_captured, vec2 p_mouse_delta, float p_dt);
};

}
