#pragma once
#include <core/rendering/debug_draw.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <editor/application/editor_context.h>
#include <editor/viewport/shape_gizmo.h>
#include <imgui.h>
#include <functional>

namespace lumen {

struct PreviewViewport
{
    static constexpr float LOOK_CLICK_SLOP = 3.0f;

    drivers::CursorLockWin32 cursor_lock;
    std::function<void(DebugDraw&)> debug_lines;

    bool allow_grid = true;
    ShapeGizmo shape_gizmo;
    ImVec2 view_min = ImVec2(0.0f, 0.0f);
    ImVec2 view_size = ImVec2(0.0f, 0.0f);
    bool hovered = false;
    bool picked = false;
    ImVec2 pick_pos = ImVec2(0.0f, 0.0f);

    bool looking = false;
    ImVec2 look_delta = ImVec2(0.0f, 0.0f);
    float look_zoom = 0.0f;
    bool panning = false;
    ImVec2 pan_delta = ImVec2(0.0f, 0.0f);
    ImVec2 _look_press = ImVec2(0.0f, 0.0f);
    float _look_travel = 0.0f;

    void _update_look(bool p_start, ImVec2 p_center);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void release();
    bool edit_shape(EditorContext& ctx, const ShapeEdit& p_shape, bool& r_changed);
    int32_t pick_point(EditorContext& ctx, const glm::vec3* p_points, size_t p_count, float p_radius_px) const;
};

}
