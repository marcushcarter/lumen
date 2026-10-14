#pragma once
#include <core/world/camera.h>
#include <core/world/components.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

struct TransformGizmo
{
    static constexpr float MOVE_SNAP = 1.0f;
    static constexpr float ROTATE_SNAP = 15.0f;
    static constexpr float SCALE_SNAP = 0.1f;
    static constexpr float MIN_SCALE = 1e-3f;
    static constexpr float MIN_SCALE_FACTOR = 0.01f;
    static constexpr uint32_t RING_SEGMENTS = 64;

    enum class Handle : uint8_t {
        NONE,
        MOVE_X, MOVE_Y, MOVE_Z,
        MOVE_SCREEN,
        ROTATE_X, ROTATE_Y, ROTATE_Z,
        SCALE_X, SCALE_Y, SCALE_Z,
    };

    struct Frame
    {
        mat4 view_proj;
        vec3 cam_position;
        quat cam_rotation;
        float near_z;
        float proj_x;
        float proj_y;
        ImVec2 min;
        ImVec2 size;
    };

    struct Shape
    {
        ImVec2 center;
        ImVec2 tip[3];
        ImVec2 scale[3];
        bool tip_visible[3];
        bool scale_visible[3];
        ImVec2 ring[3][RING_SEGMENTS];
        bool ring_visible[3][RING_SEGMENTS];
    };

    Handle hovered = Handle::NONE;
    Handle active = Handle::NONE;

    vec3 axes[3];
    vec3 start_position;
    quat start_rotation;
    vec3 start_scale;
    vec3 start_hit;
    vec3 drag_axis;
    vec3 drag_plane_normal;
    ImVec2 start_mouse;
    ImVec2 scale_dir;
    float scale_len = 1.0f;
    float rotate_sign = 1.0f;
    float rotate_prev = 0.0f;
    float rotate_total = 0.0f;
    uint32_t scale_axis = 0;

    bool update(const Frame& p_frame, bool p_world, bool p_hover_ok, TransformComponent& r_xf, bool& r_changed);
    void reset();

    bool _project(const Frame& p_frame, vec3 p_world, ImVec2& r_screen) const;
    vec3 _ray_dir(const Frame& p_frame, ImVec2 p_mouse) const;
    bool _ray_plane(const Frame& p_frame, ImVec2 p_mouse, vec3 p_point, vec3 p_normal, vec3& r_hit) const;
    void _build_shape(const Frame& p_frame, vec3 p_pivot, float p_world_px, Shape& r_shape) const;
    Handle _hit_test(const Shape& p_shape, ImVec2 p_mouse, bool p_uniform) const;
    void _begin(const Frame& p_frame, const Shape& p_shape, vec3 p_pivot, const TransformComponent& p_xf, ImVec2 p_mouse);
    bool _drag(const Frame& p_frame, const Shape& p_shape, ImVec2 p_mouse, bool p_snap, bool p_uniform, TransformComponent& r_xf);
    void _draw(ImDrawList* p_dl, const Shape& p_shape, bool p_uniform) const;
};

}