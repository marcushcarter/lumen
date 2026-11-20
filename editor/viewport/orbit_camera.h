#pragma once
#include <core/scene/camera.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace lumen {

struct OrbitCamera
{
    static constexpr float DEFAULT_YAW = glm::half_pi<float>();
    static constexpr float MOVE_SPEED = 1.0f;
    static constexpr float MIN_MOVE_SPEED = 1.0f;
    static constexpr float MIN_SPEED = 0.05f;
    static constexpr float MAX_SPEED = 20.0f;

    struct Input {
        glm::vec2 rotate = glm::vec2(0.0f);
        glm::vec2 pan = glm::vec2(0.0f);
        float wheel = 0.0f;
        bool keys = false;
    };

    Camera camera;

    glm::vec3 target = glm::vec3(0.0f, 0.5f, 0.0f);
    float distance = 3.0f;
    float yaw = DEFAULT_YAW;
    float pitch = 0.3f;
    float speed = 1.0f;
    float rotate_speed = 0.005f;
    float zoom_step = 0.1f;
    float min_distance = 0.01f;
    float max_distance = 50.0f;
    bool keep_view = false;

    void rotate(float p_dx, float p_dy);
    void zoom(float p_wheel);
    void pan(float p_dx, float p_dy, float p_view_height);
    void move(const glm::vec3& p_axes, float p_dt);
    void control(const Input& p_input, float p_view_height, float p_dt);
    void frame(glm::vec3 p_center, float p_radius);
    void look_from(glm::vec3 p_position, glm::vec3 p_target);
    void place(const Camera& p_camera);
    void update();
};

struct OrbitDrag
{
    bool rotating = false;
    bool panning = false;

    OrbitCamera::Input update(bool p_hovered, bool p_can_start);
    void release() { rotating = panning = false; }
    bool active() const { return rotating || panning; }
};

}
