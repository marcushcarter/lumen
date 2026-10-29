#pragma once
#include <core/world/camera.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace lumen {

struct OrbitCamera
{
    // looks at the target from +X, the same side the world tab's editor camera starts on
    static constexpr float DEFAULT_YAW = glm::half_pi<float>();

    Camera camera;

    glm::vec3 target = glm::vec3(0.0f, 0.5f, 0.0f);
    float distance = 3.0f;
    float yaw = DEFAULT_YAW;
    float pitch = 0.3f;
    float rotate_speed = 0.005f;
    float zoom_step = 0.1f;
    float min_distance = 0.01f;
    float max_distance = 50.0f;

    void rotate(float p_dx, float p_dy);
    void zoom(float p_wheel);
    void frame(glm::vec3 p_center, float p_radius);
    void update();
};

}
