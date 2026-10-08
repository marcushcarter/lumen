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
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <cstring>
#include <cstdio>

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

void CenterView::_draw_scene(EditorContext& ctx)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    uint32_t picked = PickFeature::NONE;
    if (ctx.render_path && ctx.world && ctx.selected && ctx.render_path->pick.take_result(picked)) {
        World& world = *ctx.world;
        *ctx.selected = ENTITY_NULL;
        if (picked != PickFeature::NONE && picked < world.generations.size()) {
            const Entity e{ picked, world.generations[picked] };
            if (world.has<EntityIdComponent>(e)) *ctx.selected = e;
        }
    }

    if (!ImGui::IsAnyItemActive()) {
        ctx.renderer->request_size((uint32_t)(size.x * screen_percentage), (uint32_t)(size.y * screen_percentage));
    }
    
    RenderGraph::ImageResource* sel = ctx.renderer->graph.image_resource("Viewport");
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    
    VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);
    if (set) {
        ImGui::Image((ImTextureID)set, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
        if (!playing && ctx.render_path && ImGui::IsItemClicked(ImGuiMouseButton_Left) && size.x > 0.0f && size.y > 0.0f) {
            const ImVec2 m = ImGui::GetMousePos();
            ctx.render_path->pick.request((m.x - pos.x) / size.x, 1.0f - (m.y - pos.y) / size.y);
        }
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
    if (right_overlay.begin_menu(ICON_FA_BARS)) {
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
    right_overlay.end();

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

    // Scene / gap / drawer content / tab strip are stacked, never overlapped, so the viewport only renders visible pixels.
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
