#include <editor/docking/center_view/overlay_bar.h>
#include <drivers/imgui/imgui_helpers.h>
#include <editor/docking/tab_strip.h>
#include <imgui.h>
#include <cstdio>
#include <algorithm>

namespace lumen {

static void overlay_push_style()
{
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 28, 30, 130));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 62, 66, 190));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 82, 88, 220));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
}

static void overlay_pop_style()
{
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
}

static void _menu_push_style()
{
    const DockColors c = DockColors::get();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c.strip);
    ImGui::PushStyleColor(ImGuiCol_Border, c.line);
    ImGui::PushStyleColor(ImGuiCol_Separator, c.line);
    ImGui::PushStyleColor(ImGuiCol_Header, c.pane);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, c.tab_hovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, c.pane);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.pane);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, c.tab_hovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, c.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
}

static void _menu_pop_style()
{
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(9);
}

void OverlayBar::begin(ImVec2 p_origin, ImVec2 p_region, Align p_align, float p_margin, float p_spacing)
{
    origin = p_origin;
    region = p_region;
    align = p_align;
    margin = p_margin;
    spacing = p_spacing;
    row_y = origin.y + margin;
    cursor_x = (align == Align::LEFT) ? origin.x + margin : origin.x + region.x - margin;

    ImVec4 ac = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ac.w = 130.0f / 255.0f;
    active_col = ImGui::GetColorU32(ac);
}

void OverlayBar::end()
{

}

bool OverlayBar::_emit(const char* p_label, ImVec2 p_size, bool p_active)
{
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    float w = p_size.x, h = p_size.y;
    if (w <= 0.0f) w = ImGui::CalcTextSize(p_label, nullptr, true).x + fp.x * 2.0f;
    if (h <= 0.0f) h = ImGui::GetFrameHeight();

    float x;
    if (align == Align::LEFT) {
        x = cursor_x;
        cursor_x += w + spacing;
    } else {
        cursor_x -= w;
        x = cursor_x;
        cursor_x -= spacing;
    }

    ImGui::SetCursorScreenPos(ImVec2(x, row_y));

    int pushed = 0;
    if (p_active) { ImGui::PushStyleColor(ImGuiCol_Button, active_col); ++pushed; }
    const bool clicked = ImGui::Button(p_label, ImVec2(w, h));
    if (pushed) ImGui::PopStyleColor(pushed);
    return clicked;

}

bool OverlayBar::button(const char* p_label, ImVec2 p_size)
{
    return _emit(p_label, p_size, false);
}

bool OverlayBar::toggle(const char* p_label, bool& p_active, ImVec2 p_size)
{
    const bool clicked = _emit(p_label, p_size, p_active);
    if (clicked) p_active = !p_active;
    return clicked;
}

bool OverlayBar::combo(const char* p_id, const char* p_preview, float p_width)
{
    float x;
    if (align == Align::LEFT) {
        x = cursor_x;
        cursor_x += p_width + spacing;
    } else {
        cursor_x -= p_width;
        x = cursor_x;
        cursor_x -= spacing;
    }

    ImGui::SetCursorScreenPos(ImVec2(x, row_y));
    ImGui::SetNextItemWidth(p_width);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(28, 28, 30, 130));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(60, 62, 66, 190));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(80, 82, 88, 220));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    const bool open = ImGui::BeginCombo(p_id, p_preview, ImGuiComboFlags_HeightLargest);
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
    return open;
}

bool OverlayBar::begin_menu(const char* p_label, ImVec2 p_size)
{
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    float w = p_size.x, h = p_size.y;
    if (w <= 0.0f) w = ImGui::CalcTextSize(p_label, nullptr, true).x + fp.x * 2.0f;
    if (h <= 0.0f) h = ImGui::GetFrameHeight();

    float x;
    if (align == Align::LEFT) {
        x = cursor_x;
        cursor_x += w + spacing;
    } else {
        cursor_x -= w;
        x = cursor_x;
        cursor_x -= spacing;
    }

    ImGui::SetCursorScreenPos(ImVec2(x, row_y));

    overlay_push_style();
    int pushed = 0;
    if (ImGui::IsPopupOpen(p_label)) { ImGui::PushStyleColor(ImGuiCol_Button, active_col); ++pushed; }
    const bool clicked = ImGui::Button(p_label, ImVec2(w, h));
    if (pushed) ImGui::PopStyleColor(pushed);
    overlay_pop_style();

    const ImVec2 bmin = ImGui::GetItemRectMin();
    const ImVec2 bmax = ImGui::GetItemRectMax();
    if (clicked) ImGui::OpenPopup(p_label);

    const bool right = align == Align::RIGHT;
    const ImVec2 anchor(right ? bmax.x : bmin.x, bmax.y + 2.0f);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, 0.0f));

    _menu_push_style();
    const bool open = ImGui::BeginPopup(p_label, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) _menu_pop_style();
    return open;
}

void OverlayBar::end_menu()
{
    ImGui::EndPopup();
    _menu_pop_style();
}

void OverlayBar::gap(float p_w)
{
    if (align == Align::LEFT) cursor_x += p_w;
    else cursor_x -= p_w;
}

}