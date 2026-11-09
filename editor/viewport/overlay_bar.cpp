#include <editor/viewport/overlay_bar.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <algorithm>

namespace lumen {

static void _overlay_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.menu_field);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.menu_field_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.outline);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.overlay.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ui.stroke.hairline);
}

static void _overlay_pop_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
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

    active_col = ui.color.accent;
}

void OverlayBar::end()
{

}

float OverlayBar::_place(float p_w)
{
    float x;
    if (align == Align::LEFT) {
        x = cursor_x;
        cursor_x += p_w + spacing;
    } else {
        cursor_x -= p_w;
        x = cursor_x;
        cursor_x -= spacing;
    }
    return x;
}

bool OverlayBar::_emit(const char* p_label, ImVec2 p_size, bool p_active)
{
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    float w = p_size.x, h = p_size.y;
    if (h <= 0.0f) h = ImGui::GetFrameHeight();
    if (w <= 0.0f) w = ImMax(h, ImGui::CalcTextSize(p_label, nullptr, true).x + fp.x * 2.0f);

    ImGui::SetCursorScreenPos(ImVec2(_place(w), row_y));

    _overlay_push_style();
    int pushed = 0;
    if (p_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, active_col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui_style().color.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_Text, ui_style().color.text_hi);
        pushed = 3;
    }
    const bool clicked = ImGui::Button(p_label, ImVec2(w, h));
    if (pushed) ImGui::PopStyleColor(pushed);
    _overlay_pop_style();
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
    ImGui::SetCursorScreenPos(ImVec2(_place(p_width), row_y));
    ImGui::SetNextItemWidth(p_width);

    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.menu_field);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.menu_field_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.menu_field_hover);
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.menu_field);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.menu_field_hover);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.overlay.rounding);
    const bool open = ImGui::BeginCombo(p_id, p_preview, ImGuiComboFlags_HeightLargest);
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(6);
    return open;
}

bool OverlayBar::begin_menu(const char* p_label, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const float arrow = ui.header.dropdown_arrow;
    const bool icon_only = ImGui::CalcTextSize(p_label, end).x <= ImGui::GetFontSize() * 1.2f;
    float w = p_size.x, h = p_size.y;
    if (h <= 0.0f) h = ImGui::GetFrameHeight();
    if (w <= 0.0f) w = ImGui::CalcTextSize(p_label, end).x + fp.x * 2.0f + (icon_only ? arrow + fp.x * 0.5f : arrow * 2.0f + fp.x);

    const ImVec2 a(_place(w), row_y);
    const ImVec2 b(a.x + w, a.y + h);
    ImGui::SetCursorScreenPos(a);
    const bool open_now = ImGui::IsPopupOpen(p_label);
    const bool clicked = ImGui::InvisibleButton(p_label, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fill = open_now ? ui.color.accent : (hovered ? ui.color.menu_field_hover : ui.color.menu_field);
    dl->AddRectFilled(a, b, fill, ui.overlay.rounding);
    dl->AddRect(a, b, ui.color.outline, ui.overlay.rounding);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float ty = ImFloor(a.y + (h - ts.y) * 0.5f);
    const ImU32 text = open_now ? ui.color.text_hi : ui.color.text;
    dl->AddText(ImVec2(a.x + fp.x, ty), text, p_label, end);
    ui_chevron(dl, ImVec2(ImFloor(b.x - fp.x - arrow * 0.5f) + 0.5f, ImFloor(a.y + h * 0.5f) + 0.5f), arrow * 2.0f, ImGuiDir_Down, ui_with_alpha(text, 0.8f), ui.stroke.thin);

    if (clicked) ImGui::OpenPopup(p_label);

    const bool right = align == Align::RIGHT;
    const ImVec2 anchor(right ? b.x : a.x, b.y + ui.menu.popup_offset);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, 0.0f));

    ui_menu_push_style();
    const bool open = ImGui::BeginPopup(p_label, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) ui_menu_pop_style();
    return open;
}

bool OverlayBar::header_menu(const char* p_label)
{
    const UiStyle& ui = ui_style();
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const float h = ImGui::GetFrameHeight();
    const float w = ImGui::CalcTextSize(p_label, end).x + ui.header.menu_pad_x * 2.0f;
    const ImVec2 a(_place(w), row_y);
    const ImVec2 b(a.x + w, a.y + h);

    ImGui::SetCursorScreenPos(a);
    const ImGuiID popup_id = ImGui::GetID(p_label);
    const bool open_now = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    if (pressed && !open_now) ImGui::OpenPopupEx(popup_id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (open_now || hovered) dl->AddRectFilled(a, b, ui.color.header_hover, ui.header.rounding);
    ui_text_centered(dl, a, b, ui.color.pulldown_text, p_label, end);

    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + ui.menu.popup_offset), ImGuiCond_Always);
    ui_menu_push_style();
    const bool open = ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) ui_menu_pop_style();
    return open;
}

void OverlayBar::end_menu()
{
    ImGui::EndPopup();
    ui_menu_pop_style();
}

void OverlayBar::gap(float p_w)
{
    if (p_w < 0.0f) p_w = ui_style().overlay.group_gap;
    if (align == Align::LEFT) cursor_x += p_w;
    else cursor_x -= p_w;
}

}
