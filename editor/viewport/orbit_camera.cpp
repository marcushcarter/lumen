#include <editor/viewport/orbit_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace lumen {

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

void OrbitCamera::update()
{
    const float cp = std::cos(pitch);
    const glm::vec3 offset(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
    camera.position = target + offset * distance;
    camera.rotation = glm::quatLookAt(-offset, glm::vec3(0.0f, 1.0f, 0.0f));
}

}