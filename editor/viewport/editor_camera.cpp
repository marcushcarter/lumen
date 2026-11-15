#include <editor/viewport/editor_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

using namespace glm;

vec3 EditorCamera::forward() const
{
    const float cp = std::cos(pitch);
    return vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
}

void EditorCamera::place(const Camera& p_camera)
{
    const vec3 f = normalize(p_camera.forward());
    position = p_camera.position;
    yaw = std::atan2(f.x, f.z);
    pitch = std::clamp(std::asin(std::clamp(f.y, -1.0f, 1.0f)), -MAX_PITCH, MAX_PITCH);
}

void EditorCamera::update(const Input& p_input, float p_dt)
{
    if (p_input.flying) {
        yaw = std::fmod(yaw - p_input.look.x * look_speed, two_pi<float>());
        pitch = std::clamp(pitch - p_input.look.y * look_speed, -MAX_PITCH, MAX_PITCH);
        if (p_input.wheel != 0.0f) speed = std::clamp(speed * std::pow(SPEED_STEP, p_input.wheel), MIN_SPEED, MAX_SPEED);

        const vec3 f = forward();
        const vec3 r = normalize(cross(f, vec3(0.0f, 1.0f, 0.0f)));
        const vec3 dir = r * p_input.move.x + vec3(0.0f, 1.0f, 0.0f) * p_input.move.y + f * p_input.move.z;
        if (dot(dir, dir) > 0.0f) {
            const float s = p_input.fast ? speed * FAST_SCALE : speed;
            position += normalize(dir) * s * p_dt;
        }
    }
    camera.position = position;
    camera.rotation = quatLookAt(forward(), vec3(0.0f, 1.0f, 0.0f));
}

}
