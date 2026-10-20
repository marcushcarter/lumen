#include <editor/viewport/editor_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

using namespace glm;

void EditorCamera::update(float )
{
    radius = 1.5f;
    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    camera.position = eye;
    camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
}

}