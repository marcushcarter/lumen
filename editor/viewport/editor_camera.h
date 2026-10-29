#pragma once
#include <core/world/camera.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct EditorCamera
{
    Camera camera;

    vec3 target = vec3(0.0f, 1.0f, 0.0f);
    float radius = 3.0f;
    float height = 1.0f;
    float angle = 0.0f;

    void update(float p_dt);
};

}
