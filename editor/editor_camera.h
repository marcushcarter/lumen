#pragma once
#include <core/world/camera.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct EditorCamera
{
    Camera camera;

    vec3 target = vec3(0.0f, 50.0f, 0.0f);
    float radius = 150.0f;
    float height = 50.0f;
    float orbit_speed = 0.1f;
    float angle = 0.0f;

    float zoom_time = 0.0f; float zoom_speed = 0.5f;

    void update(float p_dt);
};

}