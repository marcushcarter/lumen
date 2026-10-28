#include <core/world/camera.h>

namespace lumen {

vec3 Camera::forward() const { return rotation * vec3(0.0f, 0.0f, -1.0f); }
vec3 Camera::right() const { return rotation * vec3(1.0f, 0.0f, 0.0f); }
vec3 Camera::up() const { return rotation * vec3(0.0f, 1.0f, 0.0f); }

mat4 Camera::view() const { return mat4_cast(conjugate(rotation)) * translate(mat4(1.0f), -position); }

mat4 Camera::proj(float aspect) const
{
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][2] = near_z / (far_z - near_z);
    m[2][3] = -1.0f;
    m[3][2] = (far_z * near_z) / (far_z - near_z);
    return m;
}

mat4 Camera::view_proj(float aspect) const { return proj(aspect) * view(); }

}
