#pragma once
#include <core/world/camera.h>
#include <glm/glm.hpp>

namespace lumen {

struct OrbitCamera
{
    static constexpr float MAX_PITCH = 1.55f;

    Camera camera;
    glm::vec3 target = glm::vec3(0.0f, 50.0f, 0.0f);
    float distance = 300.0f;
    float yaw = 0.0f;
    float pitch = 0.3f;
    float rotate_speed = 0.005f;
    float zoom_step = 0.1f;
    float min_distance = 1.0f;
    float max_distance = 5000.0f;

    void rotate(float p_dx, float p_dy);
    void zoom(float p_wheel);
    void update();
};

}