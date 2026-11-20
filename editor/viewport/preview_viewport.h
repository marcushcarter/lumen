#pragma once
#include <core/rendering/debug_draw.h>
#include <editor/viewport/orbit_camera.h>
#include <editor/application/editor_context.h>
#include <editor/viewport/shape_gizmo.h>
#include <imgui.h>
#include <functional>

namespace lumen {

struct PreviewViewport
{
    std::function<void(DebugDraw&)> debug_lines;

    bool allow_grid = true;
    ShapeGizmo shape_gizmo;
    ImVec2 view_min = ImVec2(0.0f, 0.0f);
    ImVec2 view_size = ImVec2(0.0f, 0.0f);
    bool hovered = false;
    bool picked = false;
    ImVec2 pick_pos = ImVec2(0.0f, 0.0f);

    OrbitDrag drag;
    OrbitCamera::Input camera_input;

    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void release();
    bool edit_shape(EditorContext& ctx, const ShapeEdit& p_shape, bool& r_changed);
    int32_t pick_point(EditorContext& ctx, const glm::vec3* p_points, size_t p_count, float p_radius_px) const;
};

}
