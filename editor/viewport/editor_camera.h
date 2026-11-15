#pragma once
#include <core/scene/camera.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace lumen {

using namespace glm;

struct EditorCamera
{
    static constexpr float MAX_PITCH = 1.55f;
    static constexpr float MIN_SPEED = 0.25f;
    static constexpr float MAX_SPEED = 500.0f;
    static constexpr float SPEED_STEP = 1.2f;
    static constexpr float FAST_SCALE = 4.0f;

    Camera camera;

    vec3 position = vec3(0.0f, 2.0f, 5.0f);
    float yaw = glm::pi<float>();
    float pitch = -0.2f;
    float speed = 5.0f;
    float look_speed = 0.0025f;

    struct Input {
        bool flying = false;
        vec2 look = vec2(0.0f);
        float wheel = 0.0f;
        vec3 move = vec3(0.0f);
        bool fast = false;
    };

    vec3 forward() const;
    void place(const Camera& p_camera);
    void update(const Input& p_input, float p_dt);
};

}
