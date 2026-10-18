#pragma once
#include <core/world/camera.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct EditorCamera
{
    Camera camera;

    vec3 target = vec3(0.0f, 0.5f, 0.0f);
    float radius = 1.5f;
    float height = 0.5f;
    float orbit_speed = 0.1f;
    float angle = 0.0f;

    void update(float p_dt);
};

}