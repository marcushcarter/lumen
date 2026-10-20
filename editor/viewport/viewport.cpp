#include <editor/viewport/viewport.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <cfloat>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace lumen {

static void _view_row_decor(ImDrawList* dl, ImVec2 p, float h, const char* p_icon, const char* p_text, bool p_filled)
{
    const UiStyle::ViewMenuRow& row = ui_style().view_menu;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    const float cy = p.y + h * 0.5f;
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddCircle(ImVec2(p.x + row.indicator_x, cy), row.indicator_radius, col, 20, row.indicator_stroke);
    if (p_filled) dl->AddCircleFilled(ImVec2(p.x + row.indicator_x, cy), row.indicator_dot_radius, col, 20);

    float tx = p.x + row.label_x;
    if (p_icon) {
        const float slot = ImGui::GetFontSize() * row.icon_slot_scale;
        const float iw = ImGui::CalcTextSize(p_icon).x;
        dl->AddText(ImVec2(tx + (slot - iw) * 0.5f, ty), col, p_icon);
        tx += slot + row.icon_gap;
    }
    dl->AddText(ImVec2(tx, ty), col, p_text);
}

bool Viewport::_view_item(const char* p_icon, const char* p_name, int p_id)
{
    ImGui::PushID(p_id);
    const bool sel = (selected_view == p_id);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable("##vi", sel, 0, ImVec2(item_w, h));
    _view_row_decor(ImGui::GetWindowDrawList(), p, h, p_icon, p_name, sel);
    if (clicked) selected_view = p_id;
    ImGui::PopID();
    return clicked;
}

bool Viewport::_view_submenu(const DebugViewCategory& p_category, bool p_active)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID popup_id = window->GetID(p_category.name);
    bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);

    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushID(p_category.name);
    ImGui::Selectable("##cat", open, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(item_w, h));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    _view_row_decor(window->DrawList, p, h, p_category.icon, p_category.name, p_active);
    ImGui::RenderArrow(window->DrawList, ImVec2(p.x + item_w - g.FontSize - ui_style().view_menu.arrow_inset, p.y + (h - g.FontSize) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Right);

    if (hovered && !open) {
        ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
        open = true;
    } else if (open && !hovered && g.HoveredWindow == window && g.ActiveId == 0) {
        ImGui::ClosePopupToLevel(g.BeginPopupStack.Size, true);
        open = false;
    }
    if (!open) return false;

    ImGui::SetNextWindowPos(ImVec2(p.x, p.y - g.Style.WindowPadding.y), ImGuiCond_Always);
    return ImGui::BeginPopupMenuEx(popup_id, p_category.name, ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);
}

static void _outline_rect(EditorContext& ctx, OutlineFeature& r_outline)
{
    static constexpr size_t MAX_BOUNDED = 4096;
    const World& world = *ctx.world;
    const Renderer& renderer = *ctx.renderer;
    if (ctx.selection->entities.size() > MAX_BOUNDED || renderer.main_view.output_width == 0 || renderer.main_view.output_height == 0) {
        r_outline.set_rect(vec2(0.0f), vec2(1.0f));
        return;
    }
    const Camera& cam = renderer.main_view.camera;
    const mat4 view_proj = cam.view_proj((float)renderer.main_view.output_width / (float)renderer.main_view.output_height);
    vec2 lo(FLT_MAX);
    vec2 hi(-FLT_MAX);
    bool full = false;

    auto add_box = [&](const mat4& p_mvp, vec3 p_min, vec3 p_max) {
        vec2 box_lo(FLT_MAX);
        vec2 box_hi(-FLT_MAX);
        uint32_t behind = 0;
        for (uint32_t c = 0; c < 8; c++) {
            const vec3 corner((c & 1) ? p_max.x : p_min.x, (c & 2) ? p_max.y : p_min.y, (c & 4) ? p_max.z : p_min.z);
            const vec4 clip = p_mvp * vec4(corner, 1.0f);
            if (clip.w <= cam.near_z) {
                behind++;
                continue;
            }
            const vec2 uv = vec2(clip) / clip.w * 0.5f + 0.5f;
            box_lo = min(box_lo, uv);
            box_hi = max(box_hi, uv);
        }
        if (behind == 8) return;
        if (behind > 0) {
            full = true;
            return;
        }
        lo = min(lo, box_lo);
        hi = max(hi, box_hi);
    };

    for (const Entity e : ctx.selection->entities) {
        const TransformComponent* xf = world.try_get<TransformComponent>(e);
        if (!xf || world.has<EditorHiddenTag>(e)) continue;
        const mat4 mvp = view_proj * translate(mat4(1.0f), xf->position) * mat4_cast(xf->rotation) * glm::scale(mat4(1.0f), xf->scale);
        if (const ModelComponent* m = world.try_get<ModelComponent>(e)) {
            if (const LMesh* mesh = renderer.geometry.get(renderer.models.mesh_index(m->model))) add_box(mvp, mesh->pos_min, mesh->pos_min + mesh->pos_extent);
        }
        if (const ModelGridComponent* g = world.try_get<ModelGridComponent>(e)) {
            if (const LMesh* mesh = renderer.geometry.get(renderer.models.mesh_index(g->model))) {
                const float spacing = g->spacing > 0.0f ? g->spacing : mesh->bounds_sphere.w * 1.5f;
                const vec3 half = (vec3(g->count) - 1.0f) * 0.5f * spacing;
                add_box(mvp, mesh->pos_min - half, mesh->pos_min + mesh->pos_extent + half);
            }
        }
        if (full) break;
    }
    if (full) r_outline.set_rect(vec2(0.0f), vec2(1.0f));
    else r_outline.set_rect(lo, hi);
}

bool Viewport::_draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok)
{
    if (!ctx.world || !ctx.selection || p_size.x < 2.0f || p_size.y < 2.0f) {
        gizmo.reset();
        return false;
    }
    const Entity selected = ctx.selection->primary();
    TransformComponent* xf = ctx.world->try_get<TransformComponent>(selected);
    if (!xf) {
        gizmo.reset();
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const Camera& cam = ctx.renderer->main_view.camera;
    const mat4 proj = cam.proj(p_size.x / p_size.y);

    TransformGizmo::Frame frame;
    frame.view_proj = proj * cam.view();
    frame.cam_position = cam.position;
    frame.cam_rotation = cam.rotation;
    frame.near_z = cam.near_z;
    frame.proj_x = proj[0][0];
    frame.proj_y = proj[1][1];
    frame.min = p_pos;
    frame.size = p_size;

    const bool input_ok = ImGui::IsMousePosValid() && !io.AppFocusLost && !io.WantTextInput;
    bool changed = false;
    const bool hot = gizmo.update(frame, gizmo_world, p_hover_ok && input_ok, *xf, changed);
    if (changed) ctx.world->touch(selected);
    return hot;
}

void Viewport::_draw_image(EditorContext& ctx)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    uint32_t picked = PickFeature::NONE;
    if (allow_pick && ctx.render_path && ctx.world && ctx.selection && ctx.render_path->pick.take_result(picked)) {
        World& world = *ctx.world;
        ctx.selection->clear();
        if (picked != PickFeature::NONE && picked < world.generations.size()) {
            const Entity e{ picked, world.generations[picked] };
            if (world.has<EntityIdComponent>(e)) ctx.selection->select(e);
        }
    }
    if (allow_outline && ctx.render_path && ctx.selection && ctx.render_path->outline.source_version != ctx.selection->version) {
        ctx.render_path->outline.set_selection(ctx.selection->entities.data(), (uint32_t)ctx.selection->entities.size());
        ctx.render_path->outline.source_version = ctx.selection->version;
    }
    if (allow_outline && ctx.render_path && ctx.world && ctx.selection && ctx.render_path->outline.enabled && !ctx.selection->entities.empty()) _outline_rect(ctx, ctx.render_path->outline);

    if (!ImGui::IsAnyItemActive() && size.x >= 1.0f && size.y >= 1.0f) {
        const uint32_t w = (uint32_t)ImClamp(size.x * screen_percentage, 1.0f, 8192.0f);
        const uint32_t h = (uint32_t)ImClamp(size.y * screen_percentage, 1.0f, 8192.0f);
        ctx.renderer->request_size(w, h);
    }
    
    if (ctx.render_path) {
        const DebugView& view = DEBUG_VIEWS[selected_view];
        const bool lit = view.op == DebugViewOp::COPY && view.image && std::strcmp(view.image, "DisplayColor") == 0;
        ctx.render_path->taa.suppressed = !lit;
        ctx.renderer->main_view.jitter = ctx.render_path->taa.enabled && lit;
    }

    const char* vp_image = ctx.render_path ? ctx.render_path->ui.viewport_image(ctx.renderer->main_view.graph) : nullptr;
    RenderGraph::ImageResource* sel = vp_image ? ctx.renderer->main_view.graph.image_resource(vp_image) : nullptr;
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    
    VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);
    bool pick_click = false;
    bool look_click = false;
    bool look_hover = false;
    ImVec2 pick_mouse = ImGui::GetMousePos();
    if (set) {
        ImGui::Image((ImTextureID)set, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const bool image_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool image_hovered = ImGui::IsItemHovered();
        const bool playing = false;
        const bool gizmo_hover_ok = image_hovered && !popup_open_last_frame && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool gizmo_hot = !playing && allow_gizmo && _draw_gizmo(ctx, pos, size, gizmo_hover_ok);
        pick_click = allow_pick && !playing && !gizmo_hot && image_clicked && size.x > 0.0f && size.y > 0.0f;
        look_click = allow_look && !gizmo_hot && image_clicked;
        look_hover = allow_look && image_hovered;
    } else {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ui_style().color.sunken);
    }

    const DebugView& cur = DEBUG_VIEWS[selected_view];
    const char* btn_icon = cur.category ? cur.category->icon : cur.icon;
    char view_btn[128];
    if (btn_icon) snprintf(view_btn, sizeof(view_btn), "%s  %s###ViewMode", btn_icon, cur.name);
    else snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", cur.name);

    left_overlay.begin(pos, size, OverlayBar::Align::LEFT);
    if (left_overlay.begin_menu(ICON_FA_BARS)) {
        ImGui::SliderFloat("Viewport Resolution", &screen_percentage, 0.01f, 1.0f);
        float internal = ctx.renderer->main_view.render_scale * 100.0f;
        if (ImGui::SliderFloat("Internal Resolution", &internal, RenderView::MIN_RENDER_SCALE * 100.0f, RenderView::MAX_RENDER_SCALE * 100.0f, "%.0f%%")) ctx.renderer->main_view.render_scale = internal / 100.0f;
        ImGui::SliderFloat("LOD Bias", &ctx.renderer->main_view.lod_bias, 0.25f, 4.0f);
        if (ctx.render_path) {
            GeometryFeature& geo = ctx.render_path->geometry;
            ImGui::Checkbox("Occlusion Culling", &geo.occlusion);
            ImGui::Checkbox("Contribution Culling", &geo.contribution_culling);
            ImGui::SliderFloat("Min Screen Radius (px)", &geo.contribution_px, 0.25f, 8.0f);
            ImGui::Checkbox("Mesh Shading", &geo.mesh_shading);
            ImGui::Checkbox("Task Shading", &geo.task_shading);
            ImGui::Separator();
            ImGui::Checkbox("TAA", &ctx.render_path->taa.enabled);
            ImGui::SliderFloat("Sharpness", &ctx.render_path->taa.sharpness, 0.0f, 2.0f);
            static const char* TONEMAPPERS[PostProcessFeature::TONEMAPPER_COUNT] = { "AgX", "ACES", "PBR Neutral", "None" };
            int tonemapper = (int)ctx.render_path->post.tonemapper;
            if (ImGui::Combo("Tonemapper", &tonemapper, TONEMAPPERS, PostProcessFeature::TONEMAPPER_COUNT)) ctx.render_path->post.tonemapper = (PostProcessFeature::Tonemapper)tonemapper;
            ImGui::Checkbox("Auto Exposure", &ctx.render_path->exposure.enabled);
        }
        left_overlay.end_menu();
    }
    left_overlay.gap();
    if (left_overlay.begin_menu(view_btn)) {
        int i = 0;
        while (i < DEBUG_VIEW_COUNT) {
            const DebugView& d = DEBUG_VIEWS[i];
            if (!d.category) { _view_item(d.icon, d.name, i); i++; continue; }

            int j = i;
            bool active = false;
            while (j < DEBUG_VIEW_COUNT && DEBUG_VIEWS[j].category == d.category) {
                if (j == selected_view) active = true;
                j++;
            }
            if (_view_submenu(*d.category, active)) {
                for (int k = i; k < j; k++) _view_item(nullptr, DEBUG_VIEWS[k].name, k);
                ImGui::EndMenu();
            }
            i = j;
        }
        left_overlay.end_menu();
    }
    left_overlay.end();

    if (allow_gizmo) {
        right_overlay.begin(pos, size, OverlayBar::Align::RIGHT);
        right_overlay.toggle(gizmo_world ? ICON_FA_GLOBE "  World###gizmo_space" : ICON_FA_CUBE "  Object###gizmo_space", gizmo_world);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(gizmo_world ? "World space" : "Object space");
        right_overlay.end();
    }

    if (pick_click && ctx.render_path && !ImGui::IsAnyItemHovered() && !popup_open_last_frame) {
        ctx.render_path->pick.request((pick_mouse.x - pos.x) / size.x, 1.0f - (pick_mouse.y - pos.y) / size.y);
    }
    look_zoom = (look_hover && !ImGui::IsAnyItemHovered() && !popup_open_last_frame) ? ImGui::GetIO().MouseWheel : 0.0f;
    _update_look(look_click && !ImGui::IsAnyItemHovered() && !popup_open_last_frame, ImVec2(ImFloor(pos.x + size.x * 0.5f), ImFloor(pos.y + size.y * 0.5f)));
    popup_open_last_frame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (ctx.render_path) ctx.render_path->debug.view = (uint32_t)selected_view;
}

void Viewport::_update_look(bool p_start, ImVec2 p_center)
{
    look_delta = ImVec2(0.0f, 0.0f);
    if (!looking && p_start) {
        looking = true;
        _look_press = ImGui::GetMousePos();
        _look_travel = 0.0f;
        cursor_lock.begin();
    }
    if (!looking) return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        looking = false;
        if (_look_travel > LOOK_CLICK_SLOP) cursor_lock.end(p_center.x - _look_press.x, p_center.y - _look_press.y);
        else cursor_lock.end();
        return;
    }
    cursor_lock.take_delta(look_delta.x, look_delta.y);
    _look_travel += std::fabs(look_delta.x) + std::fabs(look_delta.y);
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

void Viewport::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##scene", ImVec2(ImMax(1.0f, p_max.x - p_min.x), ImMax(1.0f, p_max.y - p_min.y)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    _draw_image(ctx);
    ImGui::EndChild();
}

}