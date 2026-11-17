#include <editor/viewport/preview_viewport.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/ui_style.h>
#include <editor/viewport/viewport.h>
#include <core/rendering/features/editor/debug_view.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lumen {

void PreviewViewport::release()
{
    cursor_lock.end();
    looking = false;
    panning = false;
    shape_gizmo.reset();
}

bool PreviewViewport::edit_shape(EditorContext& ctx, const ShapeEdit& p_shape, bool& r_changed)
{
    r_changed = false;
    if (!ctx.renderer || view_size.x < 2.0f || view_size.y < 2.0f) return false;
    const ImGuiIO& io = ImGui::GetIO();
    const bool input_ok = hovered && !looking && ImGui::IsMousePosValid() && !io.AppFocusLost && !io.WantTextInput;
    const TransformGizmo::Frame frame = ShapeGizmo::frame(ctx.renderer->main_view.camera, view_min, view_size);
    return shape_gizmo.update(frame, mat4(1.0f), false, input_ok, p_shape, r_changed);
}

int32_t PreviewViewport::pick_point(EditorContext& ctx, const glm::vec3* p_points, size_t p_count, float p_radius_px) const
{
    if (!ctx.renderer || view_size.x < 2.0f || view_size.y < 2.0f) return -1;
    const Camera& camera = ctx.renderer->main_view.camera;
    const mat4 view_proj = camera.view_proj(view_size.x / view_size.y);
    int32_t best = -1;
    float best_d2 = p_radius_px * p_radius_px;
    float best_w = FLT_MAX;
    for (size_t i = 0; i < p_count; i++) {
        const vec4 clip = view_proj * vec4(p_points[i], 1.0f);
        if (clip.w <= camera.near_z) continue;
        const float x = view_min.x + (clip.x / clip.w * 0.5f + 0.5f) * view_size.x;
        const float y = view_min.y + (0.5f - clip.y / clip.w * 0.5f) * view_size.y;
        const float d2 = (x - pick_pos.x) * (x - pick_pos.x) + (y - pick_pos.y) * (y - pick_pos.y);
        if (d2 > best_d2 || (d2 == best_d2 && clip.w >= best_w)) continue;
        best_d2 = d2;
        best_w = clip.w;
        best = (int32_t)i;
    }
    return best;
}

void PreviewViewport::_update_look(bool p_start, ImVec2 p_center)
{
    look_delta = ImVec2(0.0f, 0.0f);
    if (!looking && p_start) {
        looking = true;
        _look_press = ImGui::GetMousePos();
        _look_travel = 0.0f;
        cursor_lock.begin();
    }
    if (!looking) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        looking = false;
        if (_look_travel > LOOK_CLICK_SLOP) cursor_lock.end(p_center.x - _look_press.x, p_center.y - _look_press.y);
        else cursor_lock.end();
        return;
    }
    cursor_lock.take_delta(look_delta.x, look_delta.y);
    _look_travel += std::fabs(look_delta.x) + std::fabs(look_delta.y);
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

void PreviewViewport::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p_min, p_max, ui.color.viewport_back);
    if (size.x < 2.0f || size.y < 2.0f || !ctx.renderer) return;

    ctx.renderer->request_size((uint32_t)ImClamp(size.x, 1.0f, 8192.0f), (uint32_t)ImClamp(size.y, 1.0f, 8192.0f));
    const ViewportView& view = viewport_view();
    if (EditorRenderPath* path = ctx.render_path) {
        const ImVec4 gc = ui_vec4(ui.color.grid);
        const ImVec4 xc = ui_vec4(ui.color.axis_x);
        const ImVec4 zc = ui_vec4(ui.color.axis_z);
        path->grid.show_grid = allow_grid;
        path->grid.show_axes = allow_grid;
        path->grid.grid_color = vec4(gc.x, gc.y, gc.z, gc.w);
        path->grid.axis_x_color = vec4(xc.x, xc.y, xc.z, xc.w);
        path->grid.axis_z_color = vec4(zc.x, zc.y, zc.z, zc.w);
        path->outline.enabled = false;
        path->geometry.freeze = false;
        path->sun_shadow.freeze = false;
        path->debug.view = (uint32_t)std::clamp(view.debug_view, 0, DEBUG_VIEW_COUNT - 1);
        path->lines.mask = (uint32_t)DebugCategory::GENERAL | (uint32_t)DebugCategory::COLLISION | (uint32_t)DebugCategory::BONES;
        if (debug_lines) debug_lines(debug_draw());

        const char* image = path->ui.viewport_image(ctx.renderer->main_view.graph);
        RenderGraph::ImageResource* res = image ? ctx.renderer->main_view.graph.image_resource(image) : nullptr;
        VkImageView image_view = VK_NULL_HANDLE;
        if (res && res->image && res->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) image_view = res->image->image_view;
        if (const VkDescriptorSet set = ctx.imgui->texture_cache.get(image_view)) dl->AddImage((ImTextureID)set, p_min, p_max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }

    ImGui::SetCursorScreenPos(p_min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##preview", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    hovered = ImGui::IsItemHovered();
    view_min = p_min;
    view_size = size;
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right) && shape_gizmo.gizmo.active == TransformGizmo::Handle::NONE;
    picked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && shape_gizmo.gizmo.active == TransformGizmo::Handle::NONE;
    pick_pos = ImGui::GetMousePos();

    look_zoom = hovered ? ImGui::GetIO().MouseWheel : 0.0f;
    if (hovered && !looking && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning = false;
    pan_delta = panning ? ImGui::GetIO().MouseDelta : ImVec2(0.0f, 0.0f);
    _update_look(clicked, ImVec2(ImFloor(p_min.x + size.x * 0.5f), ImFloor(p_min.y + size.y * 0.5f)));
}

}
