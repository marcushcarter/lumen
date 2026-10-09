#include <editor/docking/center_view/center_view.h>
#include <editor/docking/tab_strip.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <editor/world/editor_selection.h>
#include <IconsFontAwesome6.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <cfloat>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace lumen {

void CenterView::initialize()
{
    debugger.initialize();
}

static void _view_row_decor(ImDrawList* dl, ImVec2 p, float h, const char* p_icon, const char* p_text, bool p_filled)
{
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    const float cy = p.y + h * 0.5f;
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddCircle(ImVec2(p.x + 12.0f, cy), 5.0f, col, 20, 1.5f);
    if (p_filled) dl->AddCircleFilled(ImVec2(p.x + 12.0f, cy), 2.5f, col, 20);

    float tx = p.x + 28.0f;
    if (p_icon) {
        const float slot = ImGui::GetFontSize() * 1.4f;
        const float iw = ImGui::CalcTextSize(p_icon).x;
        dl->AddText(ImVec2(tx + (slot - iw) * 0.5f, ty), col, p_icon);
        tx += slot + 4.0f;
    }
    dl->AddText(ImVec2(tx, ty), col, p_text);
}

bool CenterView::_view_item(const char* p_icon, const char* p_name, int p_id)
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

bool CenterView::_view_submenu(const DebugViewCategory& p_category, bool p_active)
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
    ImGui::RenderArrow(window->DrawList, ImVec2(p.x + item_w - g.FontSize - 4.0f, p.y + (h - g.FontSize) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Right);

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
    if (ctx.selection->entities.size() > MAX_BOUNDED || renderer.width == 0 || renderer.height == 0) {
        r_outline.set_rect(vec2(0.0f), vec2(1.0f));
        return;
    }
    const Camera& cam = renderer.active_camera;
    const mat4 view_proj = cam.view_proj((float)renderer.width / (float)renderer.height);
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
        if (const MeshComponent* m = world.try_get<MeshComponent>(e)) {
            if (const LMesh* mesh = renderer.geometry.get(renderer.geometry.find(m->mesh))) add_box(mvp, mesh->pos_min, mesh->pos_min + mesh->pos_extent);
        }
        if (const MeshGridComponent* g = world.try_get<MeshGridComponent>(e)) {
            if (const LMesh* mesh = renderer.geometry.get(renderer.geometry.find(g->mesh))) {
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

bool CenterView::_draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size)
{
    if (!ctx.world || !ctx.selection || p_size.x <= 0.0f || p_size.y <= 0.0f) return false;
    const Entity selected = ctx.selection->primary();
    TransformComponent* xf = ctx.world->try_get<TransformComponent>(selected);
    if (!xf) return false;

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && !ImGuizmo::IsUsing() && !io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmo_op = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmo_op = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmo_op = ImGuizmo::SCALE;
    }

    const Camera& cam = ctx.renderer->active_camera;
    const mat4 view = cam.view();
    const mat4 proj = cam.proj(p_size.x / p_size.y);
    mat4 model = translate(mat4(1.0f), xf->position) * mat4_cast(xf->rotation) * glm::scale(mat4(1.0f), xf->scale);

    const vec4 pivot_clip = proj * view * vec4(xf->position, 1.0f);
    if (pivot_clip.w <= cam.near_z && !ImGuizmo::IsUsing()) return false;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(p_pos.x, p_pos.y, p_size.x, p_size.y);

    const float step = gizmo_op == ImGuizmo::ROTATE ? 15.0f : (gizmo_op == ImGuizmo::SCALE ? 0.1f : 1.0f);
    const float snap[3] = { step, step, step };
    const ImGuizmo::MODE mode = gizmo_world ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

    if (p_size.x < 2.0f || p_size.y < 2.0f) return false;

    if (ImGuizmo::Manipulate(value_ptr(view), value_ptr(proj), gizmo_op, mode, value_ptr(model), nullptr, io.KeyCtrl ? snap : nullptr)) {
        bool finite = true;
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) finite = finite && std::isfinite(model[c][r]);
        if (!finite) return ImGuizmo::IsOver() || ImGuizmo::IsUsing();

        vec3 axis[3] = { vec3(model[0]), vec3(model[1]), vec3(model[2]) };
        vec3 s = vec3(length(axis[0]), length(axis[1]), length(axis[2]));
        if (dot(cross(axis[0], axis[1]), axis[2]) < 0.0f) {
            s.x = -s.x;
        }
        switch (gizmo_op) {
            case ImGuizmo::TRANSLATE: xf->position = vec3(model[3]); break;
            case ImGuizmo::ROTATE: if (s.x != 0.0f && s.y != 0.0f && s.z != 0.0f) xf->rotation = normalize(quat_cast(mat3(axis[0] / s.x, axis[1] / s.y, axis[2] / s.z))); break;
            case ImGuizmo::SCALE: xf->scale = s; break;
            default: break;
        }
        ctx.world->touch(selected);
    }
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

void CenterView::_draw_scene(EditorContext& ctx)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGuizmo::BeginFrame();

    uint32_t picked = PickFeature::NONE;
    if (ctx.render_path && ctx.world && ctx.selection && ctx.render_path->pick.take_result(picked)) {
        World& world = *ctx.world;
        ctx.selection->clear();
        if (picked != PickFeature::NONE && picked < world.generations.size()) {
            const Entity e{ picked, world.generations[picked] };
            if (world.has<EntityIdComponent>(e)) ctx.selection->select(e);
        }
    }
    if (ctx.render_path) ctx.render_path->outline.enabled = !(ctx.pie_is_playing && ctx.pie_is_playing());
    if (ctx.render_path && ctx.selection && ctx.render_path->outline.source_version != ctx.selection->version) {
        ctx.render_path->outline.set_selection(ctx.selection->entities.data(), (uint32_t)ctx.selection->entities.size());
        ctx.render_path->outline.source_version = ctx.selection->version;
    }
    if (ctx.render_path && ctx.world && ctx.selection && ctx.render_path->outline.enabled && !ctx.selection->entities.empty()) _outline_rect(ctx, ctx.render_path->outline);

    if (!ImGui::IsAnyItemActive() && size.x >= 1.0f && size.y >= 1.0f) {
        const uint32_t w = (uint32_t)ImClamp(size.x * screen_percentage, 1.0f, 8192.0f);
        const uint32_t h = (uint32_t)ImClamp(size.y * screen_percentage, 1.0f, 8192.0f);
        ctx.renderer->request_size(w, h);
    }
    
    RenderGraph::ImageResource* sel = ctx.renderer->graph.image_resource("Viewport");
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    
    VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);
    bool pick_click = false;
    ImVec2 pick_mouse = ImGui::GetMousePos();
    if (set) {
        ImGui::Image((ImTextureID)set, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const bool image_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
        const bool gizmo_hot = !playing && _draw_gizmo(ctx, pos, size);
        pick_click = !playing && !gizmo_hot && image_clicked && size.x > 0.0f && size.y > 0.0f;
    } else {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(25, 25, 25, 255));
    }

    const DebugView& cur = DEBUG_VIEWS[selected_view];
    const char* btn_icon = cur.category ? cur.category->icon : cur.icon;
    char view_btn[128];
    if (btn_icon) snprintf(view_btn, sizeof(view_btn), "%s  %s###ViewMode", btn_icon, cur.name);
    else snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", cur.name);

    left_overlay.begin(pos, size, OverlayBar::Align::LEFT);
    if (left_overlay.begin_menu(ICON_FA_BARS)) {
        ImGui::SliderFloat("Viewport Resolution", &screen_percentage, 0.01f, 1.0f);
        ImGui::SliderFloat("LOD Bias", &ctx.renderer->lod_bias, 0.25f, 4.0f);
        if (ctx.render_path) {
            GeometryFeature& geo = ctx.render_path->geometry;
            ImGui::Checkbox("Occlusion Culling", &geo.occlusion);
            ImGui::Checkbox("Contribution Culling", &geo.contribution_culling);
            ImGui::SliderFloat("Min Screen Radius (px)", &geo.contribution_px, 0.25f, 8.0f);
            ImGui::Checkbox("Mesh Shading", &geo.mesh_shading);
            ImGui::Checkbox("Task Shading", &geo.task_shading);
        }
        right_overlay.end_menu();
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

    right_overlay.begin(pos, size, OverlayBar::Align::RIGHT);
    right_overlay.toggle(gizmo_world ? ICON_FA_GLOBE "###gizmo_space" : ICON_FA_CUBE "###gizmo_space", gizmo_world);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(gizmo_world ? "World space" : "Object space");
    right_overlay.end();

    if (pick_click && ctx.render_path && !ImGui::IsAnyItemHovered() && !popup_open_last_frame) {
        ctx.render_path->pick.request((pick_mouse.x - pos.x) / size.x, 1.0f - (pick_mouse.y - pos.y) / size.y);
    }
    popup_open_last_frame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (ctx.render_path) ctx.render_path->debug.view = (uint32_t)selected_view;
}

void CenterView::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const DockColors colors = DockColors::get();
    const float w = p_max.x - p_min.x;
    const float h = p_max.y - p_min.y;
    const float strip_h = TabStrip::height();
    const float min_debug = 0.025f;
    const float max_debug = 0.7f;

    const float usable = ImMax(1.0f, h - strip_h - DOCK_GAP);
    float scene_h, content_h;
    if (debugger.collapsed) {
        scene_h = usable;
        content_h = 0.0f;
    } else {
        float min_r = 1.0f - max_debug;
        float max_r = 1.0f - min_debug;
        if (max_r < min_r) max_r = min_r;
        split_ratio = ImClamp(split_ratio, min_r, max_r);
        scene_h = ImFloor(usable * split_ratio);
        content_h = usable - scene_h;
    }

    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##scene", ImVec2(w, ImMax(1.0f, scene_h)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    _draw_scene(ctx);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + scene_h));
    SplitterState s = imgui_splitter("##vsplit", SplitAxis::Y, ImVec2(w, DOCK_GAP), 0.0f);
    if (s.active) {
        if (debugger.collapsed && s.activated) split_ratio = 1.0f;
        split_ratio += s.delta / usable;
        debugger.collapsed = (1.0f - split_ratio < min_debug);
    }

    if (!debugger.collapsed && content_h >= 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + scene_h + DOCK_GAP));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.pane);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::BeginChild("##drawer", ImVec2(w, content_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        debugger.draw_content(ctx);
        ImGui::EndChild();
    }

    const bool was_collapsed = debugger.collapsed;
    debugger.draw_strip(ImVec2(p_min.x, p_max.y - strip_h), p_max);
    if (was_collapsed && !debugger.collapsed) split_ratio = 1.0f - max_debug / 3.0f;
}

}
