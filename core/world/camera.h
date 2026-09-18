#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace lumen {

using namespace glm;

struct Camera
{
    vec3 position = vec3(0.0f, 0.0f, 5.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    float fov_y = radians(60.0f);
    float near_z = 0.1f;
    float far_z = 10000.0f;

    vec3 forward() const;
    vec3 right() const;
    vec3 up() const;

    mat4 view() const;
    mat4 proj(float aspect) const;
    mat4 view_proj(float aspect) const;
};

}
