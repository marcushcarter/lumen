#include <editor/editor_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

using namespace glm;

void EditorCamera::update(float )
{
    // angle += orbit_speed * p_dt;

    // zoom_time += zoom_speed * p_dt;
    // const float t = 0.5f * (std::sin(zoom_time / 4) + 1.0f);
    // radius = mix(10.0f, 1000.0f, t);
    
    radius = 1000.0f;

    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    camera.position = eye;
    camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
}

}
