#include <editor/docking/center_view/center_view.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
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

    if (!ImGui::IsAnyItemActive()) {
        ctx.renderer->request_size((uint32_t)(size.x * screen_percentage), (uint32_t)(size.y * screen_percentage));
    }
    
    RenderGraph::ImageResource* sel = ctx.renderer->graph.image_resource("Viewport");
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    
    VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);
    if (set) {
        ImGui::Image((ImTextureID)set, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(25, 25, 25, 255));
    }

    const DebugView& cur = DEBUG_VIEWS[selected_view];
    const char* btn_icon = cur.category ? cur.category->icon : cur.icon;
    char view_btn[128];
    // snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", DEBUG_VIEWS[selected_view].name);
    if (btn_icon) snprintf(view_btn, sizeof(view_btn), "%s  %s###ViewMode", btn_icon, cur.name);
    else snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", cur.name);

    left_overlay.begin(pos, size, OverlayBar::Align::LEFT);
    if (left_overlay.begin_menu(view_btn)) {
        int i = 0;
        while (i < DEBUG_VIEW_COUNT) {
            const DebugView& d = DEBUG_VIEWS[i];
            // if (d.category[0] == '\0') { _view_item(d.name, i); i++; continue; }
            if (!d.category) { _view_item(d.icon, d.name, i); i++; continue; }

            int j = i;
            bool active = false;
            // while (j < DEBUG_VIEW_COUNT && strcmp(DEBUG_VIEWS[j].category, d.category) == 0) {
            while (j < DEBUG_VIEW_COUNT && DEBUG_VIEWS[j].category == d.category) {
                if (j == selected_view) active = true;
                j++;
            }
            // if (_view_submenu(d.category, active)) {
            //     for (int k = i; k < j; k++) _view_item(DEBUG_VIEWS[k].name, k);
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
        if (ctx.render_path) {
            GeometryFeature& geo = ctx.render_path->geometry;
            ImGui::Checkbox("Occlusion Culling", &geo.occlusion);
        }
        right_overlay.end_menu();
    }
    right_overlay.end();

    if (ctx.render_path) ctx.render_path->debug.view = (uint32_t)selected_view;
}

void CenterView::draw(EditorContext& ctx)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float strip_h = ImGui::GetFrameHeight();
    const float handle_h = 6.0f;
    float above_h = avail.y - strip_h;
    if (above_h < 1.0f) above_h = 1.0f;

    float usable = above_h - handle_h;
    if (usable < 1.0f) usable = 1.0f;

    const float min_debug = 0.025f;
    const float max_debug = 0.7f;

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
    
    ImGui::BeginChild("##top", ImVec2(avail.x, above_h), false, ImGuiWindowFlags_NoScrollbar);
    _draw_scene(ctx);
    ImGui::EndChild();
    const ImVec2 strip_pos = ImGui::GetCursorScreenPos();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + scene_h));
    ImGui::BeginChild("##overlay", ImVec2(avail.x, handle_h + content_h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    
    SplitterState s = imgui_splitter("##vsplit", SplitAxis::Y, ImVec2(avail.x, handle_h));
    if (s.active) {
        if (debugger.collapsed && s.activated) split_ratio = 1.0f;
        split_ratio += s.delta / usable;
        debugger.collapsed = (1.0f - split_ratio < min_debug);
    }
    
    ImVec2 bmin = ImGui::GetItemRectMin();
    ImVec2 bmax = ImGui::GetItemRectMax();
    float cy = ImFloor((bmin.y + bmax.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    const float grip_w = 40.0f;
    float gx = ImFloor((bmin.x + bmax.x) * 0.5f);
    ImU32 grip_col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : s.hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    dl->AddRectFilled(ImVec2(gx - grip_w * 0.5f, cy - 2.0f), ImVec2(gx + grip_w * 0.5f, cy + 2.0f), grip_col, 2.0f);

    if (!debugger.collapsed) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::BeginChild("##bottom", ImVec2(avail.x, content_h), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
        debugger.draw_content(ctx);
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();

    ImGui::SetCursorScreenPos(strip_pos);
    const bool was_collapsed = debugger.collapsed;
    debugger.draw_strip(ctx);

    if (was_collapsed && !debugger.collapsed) split_ratio = 1.0f - max_debug / 3.0f;
}
    
}