#pragma once
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct GameInput
{
    static constexpr float MOUSE_SENSITIVITY = 0.12f;
    static constexpr float STICK_LOOK_RATE = 180.0f;
    static constexpr float STICK_DEADZONE = 0.15f;
    static constexpr float DOUBLE_TAP = 0.3f;

    vec2 move = vec2(0.0f);
    vec2 look = vec2(0.0f);
    bool sprint = false;
    bool aim = false;
    bool jump = false;
    bool crouch_toggle = false;
    bool roll = false;
    bool rotation_toggle = false;
    bool camera_toggle = false;
    bool ragdoll_toggle = false;
    bool hit_light = false;
    bool hit_heavy = false;
    bool hit_low = false;
    float alt_since = DOUBLE_TAP;

    void clear();
    void read(bool p_captured, vec2 p_mouse_delta, float p_dt);
};

}
