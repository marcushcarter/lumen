#include <editor/viewport/editor_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

using namespace glm;

void EditorCamera::update(float )
{
    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    camera.position = eye;
    camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
}

void EditorCamera::set_pose(const vec3& p_position, const quat& p_rotation)
{
    const vec3 forward = normalize(p_rotation * vec3(0.0f, 0.0f, -1.0f));
    const float reach = std::max(radius, 1.0f);
    target = p_position + forward * reach;
    height = -forward.y * reach;
    radius = std::sqrt(forward.x * forward.x + forward.z * forward.z) * reach;
    angle = std::atan2(forward.z, -forward.x);
    camera.fov_y = radians(60.0f);
    update(0.0f);
}

}
