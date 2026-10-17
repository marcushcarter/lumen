#include <editor/viewport/overlay_bar.h>
#include <drivers/imgui/imgui_helpers.h>
#include <editor/ui/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <cstdio>
#include <algorithm>

namespace lumen {

static void overlay_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.overlay);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.overlay_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.overlay_active);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.overlay.rounding);
}

static void overlay_pop_style()
{
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);
}

void OverlayBar::begin(ImVec2 p_origin, ImVec2 p_region, Align p_align, float p_margin, float p_spacing)
{
    const UiStyle& ui = ui_style();
    origin = p_origin;
    region = p_region;
    align = p_align;
    margin = p_margin >= 0.0f ? p_margin : ui.overlay.margin;
    spacing = p_spacing >= 0.0f ? p_spacing : ui.overlay.spacing;
    row_y = origin.y + margin;
    cursor_x = (align == Align::LEFT) ? origin.x + margin : origin.x + region.x - margin;

    active_col = ui_with_alpha(ImGui::GetColorU32(ImGuiCol_ButtonActive), ui.alpha.overlay_active);
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

    overlay_push_style();
    int pushed = 0;
    if (p_active) { ImGui::PushStyleColor(ImGuiCol_Button, active_col); ++pushed; }
    const bool clicked = ImGui::Button(p_label, ImVec2(w, h));
    if (pushed) ImGui::PopStyleColor(pushed);
    overlay_pop_style();
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

    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.overlay);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.overlay_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.overlay_active);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.overlay.rounding);
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
    const ImVec2 anchor(right ? bmax.x : bmin.x, bmax.y + ui_style().menu.popup_offset);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, 0.0f));

    dock_menu_push_style();
    const bool open = ImGui::BeginPopup(p_label, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) dock_menu_pop_style();
    return open;
}

void OverlayBar::end_menu()
{
    ImGui::EndPopup();
    dock_menu_pop_style();
}

void OverlayBar::gap(float p_w)
{
    if (p_w < 0.0f) p_w = ui_style().overlay.group_gap;
    if (align == Align::LEFT) cursor_x += p_w;
    else cursor_x -= p_w;
}

}