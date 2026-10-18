#include <editor/viewport/orbit_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace lumen {

static constexpr float MAX_PITCH = 1.55f;
static constexpr float FRAME_MARGIN = 1.1f;
static constexpr float MIN_RADIUS_SCALE = 0.25f;
static constexpr float MAX_FIT_SCALE = 10.0f;

void OrbitCamera::rotate(float p_dx, float p_dy)
{
    yaw -= p_dx * rotate_speed;
    pitch = glm::clamp(pitch + p_dy * rotate_speed, -MAX_PITCH, MAX_PITCH);
    yaw = std::fmod(yaw, glm::two_pi<float>());
}

void OrbitCamera::zoom(float p_wheel)
{
    distance = glm::clamp(distance * std::pow(1.0f - zoom_step, p_wheel), min_distance, max_distance);
}

void OrbitCamera::frame(glm::vec3 p_center, float p_radius)
{
    const float radius = glm::max(p_radius, 0.001f);
    const float fit = radius / std::sin(camera.fov_y * 0.5f) * FRAME_MARGIN;
    target = p_center;
    min_distance = glm::max(radius * MIN_RADIUS_SCALE, camera.near_z * 2.0f);
    max_distance = glm::max(fit * MAX_FIT_SCALE, min_distance);
    distance = glm::clamp(fit, min_distance, max_distance);
}

void OrbitCamera::update()
{
    const float cp = std::cos(pitch);
    const glm::vec3 offset(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
    camera.position = target + offset * distance;
    camera.rotation = glm::quatLookAt(-offset, glm::vec3(0.0f, 1.0f, 0.0f));
}

}