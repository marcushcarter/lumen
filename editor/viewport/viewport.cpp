#include <editor/viewport/viewport.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <core/world/prefab.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/tabs/world/scene/editor_spawn.h>
#include <editor/tabs/prefab/prefab_tab.h>
#include <editor/play/editor_play.h>
#include <core/rendering/debug_draw.h>
#include <core/animation/bone_debug.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <algorithm>
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

ViewportView& viewport_view()
{
    static ViewportView view;
    return view;
}

bool Viewport::_view_item(const char* p_icon, const char* p_name, int p_id)
{
    ImGui::PushID(p_id);
    const bool sel = (viewport_view().debug_view == p_id);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable("##vi", sel, 0, ImVec2(item_w, h));
    _view_row_decor(ImGui::GetWindowDrawList(), p, h, p_icon, p_name, sel);
    if (clicked) viewport_view().debug_view = p_id;
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

static void _outline_rect(EditorContext& ctx, OutlineFeature& r_outline, const World& p_world, const std::vector<Entity>& p_entities)
{
    static constexpr size_t MAX_BOUNDED = 4096;
    const World& world = p_world;
    const Renderer& renderer = *ctx.renderer;
    if (p_entities.size() > MAX_BOUNDED || renderer.main_view.output_width == 0 || renderer.main_view.output_height == 0) {
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

    for (const Entity e : p_entities) {
        const TransformComponent* xf = world.try_get<TransformComponent>(e);
        if (!xf || world.has<EditorHiddenTag>(e)) continue;
        const mat4 mvp = view_proj * world.world_matrix(e);
        if (const ModelComponent* m = world.try_get<ModelComponent>(e)) {
            const uint32_t mesh_index = renderer.models.mesh_index(m->model);
            const PoseComponent* pose = world.has<StaticTag>(e) ? nullptr : world.try_get<PoseComponent>(e);
            vec3 box_min, box_max;
            renderer.geometry.posed_box(mesh_index, pose ? pose->skin.data() : nullptr, pose ? (uint32_t)pose->skin.size() : 0u, box_min, box_max);
            if (renderer.geometry.get(mesh_index)) add_box(mvp, box_min, box_max);
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
    if (!ctx.world->has<TransformComponent>(selected)) {
        gizmo.reset();
        return false;
    }
    TransformComponent xf = ctx.world->world_transform(selected);

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
    const bool hot = gizmo.update(frame, gizmo_world, p_hover_ok && input_ok, xf, changed);
    if (changed) ctx.world->set_world_matrix(selected, transform_matrix(xf));
    return hot;
}

void Viewport::_draw_image(EditorContext& ctx)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool live = play && play->active();
    const bool playing = play && play->playing();
    const bool outline_on = allow_outline && !playing;

    uint32_t picked = PickFeature::NONE;
    if (allow_pick && ctx.render_path && ctx.world && ctx.selection && ctx.render_path->pick.take_result(picked)) {
        World& world = *ctx.world;
        Entity e = picked != PickFeature::NONE ? (live ? play->to_authored(picked) : world.entity_at(picked)) : ENTITY_NULL;
        const Entity root = prefab_root_of(world, e);
        const Entity primary = ctx.selection->primary();
        if (root != ENTITY_NULL && primary != e && primary != root && prefab_root_of(world, primary) != root) e = root;
        ctx.selection->clear();
        if (world.has<EntityIdComponent>(e)) ctx.selection->select(e);
    }
    if (ctx.render_path) ctx.render_path->outline.enabled = outline_on;
    if (outline_on && ctx.render_path && ctx.selection) {
        if (live) {
            _view_selection.clear();
            for (const Entity e : ctx.selection->entities) {
                const Entity r = play->to_runtime(e);
                if (r != ENTITY_NULL) _view_selection.push_back(r);
            }
            ctx.render_path->outline.set_selection(_view_selection.data(), (uint32_t)_view_selection.size());
            ctx.render_path->outline.source_version = 0;
        } else if (ctx.render_path->outline.source_version != ctx.selection->version) {
            ctx.render_path->outline.set_selection(ctx.selection->entities.data(), (uint32_t)ctx.selection->entities.size());
            ctx.render_path->outline.source_version = ctx.selection->version;
        }
    }
    if (outline_on && ctx.render_path && ctx.world && ctx.selection && !ctx.selection->entities.empty()) {
        if (live) _outline_rect(ctx, ctx.render_path->outline, play->runtime, _view_selection);
        else _outline_rect(ctx, ctx.render_path->outline, *ctx.world, ctx.selection->entities);
    }

    if (size.x >= 1.0f && size.y >= 1.0f) {
        const uint32_t w = (uint32_t)ImClamp(size.x, 1.0f, 8192.0f);
        const uint32_t h = (uint32_t)ImClamp(size.y, 1.0f, 8192.0f);
        ctx.renderer->request_size(w, h);
    }

    if (ctx.render_path) {
        GridFeature& grid = ctx.render_path->grid;
        grid.show_grid = allow_grid && !playing;
        grid.show_axes = allow_grid && !playing;
        const ImVec4 gc = ui_hex4(UiStyle::Palette::GRID);
        const ImVec4 xc = ui_hex4(UiStyle::Palette::AXIS_X);
        const ImVec4 zc = ui_hex4(UiStyle::Palette::AXIS_Z);
        grid.grid_color = vec4(gc.x, gc.y, gc.z, gc.w);
        grid.axis_x_color = vec4(xc.x, xc.y, xc.z, xc.w);
        grid.axis_z_color = vec4(zc.x, zc.y, zc.z, zc.w);

        const uint32_t debug_mask = _debug_mask();
        ctx.render_path->lines.mask = debug_mask;
        _submit_debug_draw(ctx, debug_mask, live);
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
        ImGui::Dummy(size);
        ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)set, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), IM_COL32_WHITE, ui_style().panel_box.rounding, _corners);
        const bool image_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool image_hovered = ImGui::IsItemHovered();
        const bool gizmo_hover_ok = image_hovered && !popup_open_last_frame && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool gizmo_hot = !playing && allow_gizmo && _draw_gizmo(ctx, pos, size, gizmo_hover_ok);
        pick_click = allow_pick && !allow_look && !playing && !gizmo_hot && image_clicked && size.x > 0.0f && size.y > 0.0f;
        look_click = allow_look && !gizmo_hot && image_clicked;
        look_hover = allow_look && image_hovered;
        if (allow_asset_drop && !playing) _asset_drop(ctx, pos, size);
        if (overlay) overlay(ImGui::GetWindowDrawList(), pos, size);
    } else {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ui_style().color.viewport_back, ui_style().panel_box.rounding, _corners);
    }

    if (_header_frame != ImGui::GetFrameCount()) {
        const float margin = ui_style().overlay.margin;
        _draw_controls(ctx, pos, ImVec2(pos.x + size.x, pos.y + ImGui::GetFrameHeight() + margin * 2.0f), margin);
    } else {
        _draw_options_tab(ctx, pos, size);
    }

    if (pick_click && ctx.render_path && !ImGui::IsAnyItemHovered() && !popup_open_last_frame) {
        ctx.render_path->pick.request((pick_mouse.x - pos.x) / size.x, 1.0f - (pick_mouse.y - pos.y) / size.y);
    }
    look_zoom = (look_hover && !ImGui::IsAnyItemHovered() && !popup_open_last_frame) ? ImGui::GetIO().MouseWheel : 0.0f;
    _update_look(look_click && !ImGui::IsAnyItemHovered() && !popup_open_last_frame, ImVec2(ImFloor(pos.x + size.x * 0.5f), ImFloor(pos.y + size.y * 0.5f)));
    if (allow_pick && allow_look && look_clicked && !playing && ctx.render_path && size.x > 0.0f && size.y > 0.0f) {
        ctx.render_path->pick.request((look_click_pos.x - pos.x) / size.x, 1.0f - (look_click_pos.y - pos.y) / size.y);
    }
    popup_open_last_frame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (ctx.render_path) ctx.render_path->debug.view = (uint32_t)std::clamp(viewport_view().debug_view, 0, DEBUG_VIEW_COUNT - 1);
}

void Viewport::draw_header_controls(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    _header_frame = ImGui::GetFrameCount();
    const float margin = ImFloor((p_max.y - p_min.y - ImGui::GetFrameHeight()) * 0.5f);
    _draw_controls(ctx, p_min, p_max, margin);
}

void Viewport::_draw_controls(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max, float p_margin)
{
    const ImVec2 pos = p_min;
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);
    const int selected_view = std::clamp(viewport_view().debug_view, 0, DEBUG_VIEW_COUNT - 1);
    const DebugView& cur = DEBUG_VIEWS[selected_view];
    const char* btn_icon = cur.category ? cur.category->icon : cur.icon;
    char view_btn[128];
    if (btn_icon) snprintf(view_btn, sizeof(view_btn), "%s  %s###ViewMode", btn_icon, cur.name);
    else snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", cur.name);

    right_overlay.begin(pos, size, OverlayBar::Align::RIGHT, p_margin);

    if (_header_frame != ImGui::GetFrameCount() && right_overlay.begin_menu(ICON_FA_SLIDERS "###render_settings")) {
        _draw_options(ctx);
        right_overlay.end_menu();
    }
    if (right_overlay.begin_menu(view_btn)) {
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
        right_overlay.end_menu();
    }
    {
        const bool header = _header_frame == ImGui::GetFrameCount();
        const bool open = header ? right_overlay.header_menu("View###viewport_view") : right_overlay.begin_menu("View###viewport_view");
        if (open) {
            _draw_view_menu();
            right_overlay.end_menu();
        }
    }
    right_overlay.end();
}

void Viewport::_draw_view_menu()
{
    ViewportView& view = viewport_view();
    ImGui::MenuItem("Bones", nullptr, &view.show_bones);
    ImGui::MenuItem("Collision", nullptr, &view.show_collision);
    ImGui::MenuItem("Debug Lines", nullptr, &view.show_debug_lines);
}

uint32_t Viewport::_debug_mask() const
{
    const ViewportView& view = viewport_view();
    uint32_t mask = 0;
    if (view.show_bones && (allow_debug_draw || has_bones)) mask |= (uint32_t)DebugCategory::BONES;
    if (view.show_collision && (allow_debug_draw || has_collision)) mask |= (uint32_t)DebugCategory::COLLISION;
    if (allow_debug_draw && view.show_debug_lines) mask |= (uint32_t)DebugCategory::GENERAL;
    return mask;
}

void Viewport::_submit_debug_draw(EditorContext& ctx, uint32_t p_mask, bool p_live)
{
    if (p_mask == 0) return;
    if (debug_lines) debug_lines();
    if (!allow_debug_draw) return;
    if ((p_mask & (uint32_t)DebugCategory::BONES) && ctx.renderer) {
        const World* world = p_live ? &play->runtime : ctx.world;
        if (world) debug_draw_bones(debug_draw(), *world, *ctx.renderer);
    }
}

void Viewport::_draw_options(EditorContext& ctx)
{
    ImGui::TextDisabled("Viewport");
    ImGui::PushItemWidth(ImFloor(ImGui::GetFontSize() * 9.0f));
    float internal = ctx.renderer->main_view.render_scale * 100.0f;
    if (ImGui::SliderFloat("Internal Resolution", &internal, RenderView::MIN_RENDER_SCALE * 100.0f, RenderView::MAX_RENDER_SCALE * 100.0f, "%.0f%%")) ctx.renderer->main_view.render_scale = internal / 100.0f;
    float lod_bias = ctx.renderer->main_view.lod_bias * 100.0f;
    if (ImGui::SliderFloat("LOD Bias", &lod_bias, 25.0f, 400.0f, "%.0f%%")) ctx.renderer->main_view.lod_bias = lod_bias / 100.0f;
    ImGui::PopItemWidth();
    if (options_controls) options_controls();
}

void Viewport::_draw_options_tab(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    const char* label = "Options";
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float pad = ImGui::GetStyle().FramePadding.x;
    const float arrow = ui.header.dropdown_arrow;
    const float w = ts.x + pad * 3.0f + arrow * 2.0f;
    const float h = ImGui::GetFrameHeight() + ui.space.xs;
    const float margin = ui.header.pad_x * 2.0f;
    if (p_size.x < w + margin * 2.0f || p_size.y < h) return;

    const ImVec2 a(ImFloor(p_pos.x + p_size.x - margin - w), p_pos.y);
    const ImVec2 b(a.x + w, a.y + h);
    const float r = ui.header.rounding * 1.5f;

    ImGui::SetCursorScreenPos(a);
    const ImGuiID id = ImGui::GetID("##viewport_options");
    const bool open_now = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);
    const bool pressed = ImGui::InvisibleButton("##viewport_options_tab", ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();
    if (pressed && !open_now) ImGui::OpenPopupEx(id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ui.color.header, r, ImDrawFlags_RoundCornersBottom);
    const ImVec2 ia(a.x + ui.space.xxs, a.y);
    const ImVec2 ib(b.x - ui.space.xxs, b.y - ui.space.xxs);
    if (open_now || hovered) dl->AddRectFilled(ia, ib, ui.color.header_hover, ui.header.rounding);
    const ImU32 text = (open_now || hovered) ? ui.color.text_hi : ui.color.pulldown_text;
    const float cy = ImFloor((ia.y + ib.y) * 0.5f);
    dl->AddText(ImVec2(a.x + pad, ImFloor(cy - ts.y * 0.5f)), text, label);
    ui_chevron(dl, ImVec2(ImFloor(b.x - pad - arrow * 0.5f) + 0.5f, cy + 0.5f), arrow * 2.0f, ImGuiDir_Down, ui_with_alpha(text, 0.8f), ui.stroke.thin);

    ImGui::SetNextWindowPos(ImVec2(b.x, b.y + ui.menu.popup_offset), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ui_menu_push_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.space.lg, ui.space.lg));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui.space.lg, ui.space.sm));
    if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        _draw_options(ctx);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ui_menu_pop_style();
}

void Viewport::_asset_drop(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size)
{
    if (!ctx.world || !ctx.selection) return;
    if (!ImGui::BeginDragDropTargetCustom(ImRect(p_pos, ImVec2(p_pos.x + p_size.x, p_pos.y + p_size.y)), ImGui::GetID("##asset_drop"))) return;
    if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
        const AssetDragPayload& asset = *(const AssetDragPayload*)peek->Data;
        const bool prefab_ok = asset.type == AssetType::PREFAB && !ctx.prefab_tab;
        if ((asset.type == AssetType::MODEL || prefab_ok) && asset.guid.valid() && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
            if (asset.type == AssetType::MODEL) editor_spawn(ctx, SpawnType::MODEL, ENTITY_NULL, asset.guid);
            else editor_spawn_prefab(ctx, asset.guid, ENTITY_NULL);
        }
    }
    ImGui::EndDragDropTarget();
}

void Viewport::_update_look(bool p_start, ImVec2 p_center)
{
    look_delta = ImVec2(0.0f, 0.0f);
    look_clicked = false;
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
        look_clicked = _look_travel <= LOOK_CLICK_SLOP;
        look_click_pos = _look_press;
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
    _corners = _header_frame == ImGui::GetFrameCount() ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll;
    ImGui::BeginChild("##scene", ImVec2(ImMax(1.0f, p_max.x - p_min.x), ImMax(1.0f, p_max.y - p_min.y)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    _draw_image(ctx);
    ImGui::EndChild();
}

}
