#include <editor/ui/tab_toolbar.h>
#include <editor/application/editor_context.h>
#include <editor/tabs/editor_tab.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace lumen {

static ImGuiID g_open_header_menu = 0;

void TabToolbar::begin()
{
    const UiStyle& ui = ui_style();
    min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    max = ImVec2(min.x + size.x, min.y + size.y);
    x = min.x + ui.header.pad_x;

    ImGui::PushClipRect(min, max, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, ui.color.header, ui.panel_box.rounding, ImDrawFlags_RoundCornersTop);
}

void TabToolbar::end()
{
    ImGui::PopClipRect();
}

ImVec2 TabToolbar::inner_max() const
{
    return max;
}

float TabToolbar::_row_y() const
{
    return ImFloor(min.y + (max.y - min.y - ImGui::GetFrameHeight()) * 0.5f);
}

bool TabToolbar::button(const char* p_label, const char* p_tooltip, bool p_active, bool p_enabled)
{
    const UiStyle& ui = ui_style();
    const char* label_end = ImGui::FindRenderedTextEnd(p_label);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 size(ImGui::CalcTextSize(p_label, label_end).x + ui.header.menu_pad_x * 2.0f, h);
    const ImVec2 p(x, _row_y());
    x += size.x + ui.header.item_gap;

    ImGui::SetCursorScreenPos(p);
    const bool pressed = ImGui::InvisibleButton(p_label, size);
    const bool hovered = p_enabled && ImGui::IsItemHovered();
    const bool held = p_enabled && ImGui::IsItemActive();
    if (p_tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", p_tooltip);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bmax(p.x + size.x, p.y + size.y);
    if (p_active || held) dl->AddRectFilled(p, bmax, ui.color.accent, ui.header.rounding);
    else if (hovered) dl->AddRectFilled(p, bmax, ui.color.header_hover, ui.header.rounding);

    ImU32 col = (p_active || held) ? ui.color.text_hi : ui.color.pulldown_text;
    if (!p_enabled) col = ui_with_alpha(ui.color.pulldown_text, ui.alpha.disabled);
    ui_text_centered(dl, p, bmax, col, p_label, label_end);
    return pressed && p_enabled;
}

bool TabToolbar::menu(const char* p_label, bool p_enabled)
{
    const UiStyle& ui = ui_style();
    const char* label_end = ImGui::FindRenderedTextEnd(p_label);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 size(ImGui::CalcTextSize(p_label, label_end).x + ui.header.menu_pad_x * 2.0f, h);
    const ImVec2 p(x, _row_y());
    const ImVec2 bmax(p.x + size.x, p.y + size.y);
    x += size.x + ui.header.item_gap;

    ImGui::SetCursorScreenPos(p);
    const ImGuiID popup_id = ImGui::GetID(p_label);
    const bool open_now = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const bool pressed = ImGui::InvisibleButton(p_label, size, ImGuiButtonFlags_PressedOnClick);
    const bool hovered = p_enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    if (p_enabled && pressed && !open_now) ImGui::OpenPopupEx(popup_id);
    else if (hovered && !open_now && g_open_header_menu != 0 && g_open_header_menu != popup_id && ImGui::IsPopupOpen(g_open_header_menu, ImGuiPopupFlags_None)) ImGui::OpenPopupEx(popup_id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (open_now) dl->AddRectFilled(p, bmax, ui.color.header_hover, ui.header.rounding);
    else if (hovered) dl->AddRectFilled(p, bmax, ui.color.header_hover, ui.header.rounding);
    const ImU32 col = p_enabled ? ui.color.pulldown_text : ui_with_alpha(ui.color.pulldown_text, ui.alpha.disabled);
    ui_text_centered(dl, p, bmax, col, p_label, label_end);

    ImGui::SetNextWindowPos(ImVec2(p.x, bmax.y + ui.menu.popup_offset), ImGuiCond_Always);
    ui_menu_push_style();
    const bool open = ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) {
        ui_menu_pop_style();
        return false;
    }
    g_open_header_menu = popup_id;
    return true;
}

void TabToolbar::end_menu()
{
    ImGui::EndPopup();
    ui_menu_pop_style();
}

bool TabToolbar::dropdown(const char* p_id, const char* p_preview, float p_width)
{
    const UiStyle& ui = ui_style();
    const float h = ImGui::GetFrameHeight();
    const float pad = ImGui::GetStyle().FramePadding.x;
    const float arrow = ui.header.dropdown_arrow;
    const char* preview_end = ImGui::FindRenderedTextEnd(p_preview);
    const float w = p_width > 0.0f ? p_width : ImGui::CalcTextSize(p_preview, preview_end).x + pad * 3.0f + arrow * 2.0f;
    const ImVec2 p(x, _row_y());
    const ImVec2 bmax(p.x + w, p.y + h);
    x += w + ui.header.item_gap;

    ImGui::SetCursorScreenPos(p);
    const ImGuiID popup_id = ImGui::GetID(p_id);
    const bool open_now = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();
    if (pressed && !open_now) ImGui::OpenPopupEx(popup_id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, bmax, open_now ? ui.color.accent : (hovered ? ui.color.menu_field_hover : ui.color.menu_field), ui.header.rounding);
    dl->AddRect(p, bmax, ui.color.outline, ui.header.rounding);
    const ImU32 text = open_now ? ui.color.text_hi : ui.color.text;
    const ImVec2 ts = ImGui::CalcTextSize(p_preview, preview_end);
    const ImVec2 tmin(p.x + pad, ImFloor(p.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(bmax.x - pad - arrow * 2.0f, bmax.y);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::RenderTextEllipsis(dl, tmin, tmax, tmax.x, p_preview, preview_end, &ts);
    ImGui::PopStyleColor();
    ui_chevron(dl, ImVec2(ImFloor(bmax.x - pad - arrow * 0.5f) + 0.5f, ImFloor(p.y + h * 0.5f) + 0.5f), arrow * 2.0f, ImGuiDir_Down, ui_with_alpha(text, 0.8f), ui.stroke.thin);

    ImGui::SetNextWindowPos(ImVec2(p.x, bmax.y + ui.menu.popup_offset), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ui_menu_push_style();
    const bool open = ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) ui_menu_pop_style();
    return open;
}

void TabToolbar::end_dropdown()
{
    ImGui::EndPopup();
    ui_menu_pop_style();
}

bool TabToolbar::save(EditorContext& ctx, EditorTab& r_tab)
{
    if (!button(ICON_FA_FLOPPY_DISK "  Save", "Save (Ctrl+S)")) return false;
    r_tab.save(ctx);
    return true;
}

void TabToolbar::gap()
{
    x += ui_style().header.group_gap;
}

void TabToolbar::item(float p_width)
{
    ImGui::SetCursorScreenPos(ImVec2(x, _row_y()));
    ImGui::SetNextItemWidth(p_width);
    x += p_width + ui_style().header.item_gap;
}

void TabToolbar::text(const char* p_text)
{
    const UiStyle& ui = ui_style();
    const ImVec2 size = ImGui::CalcTextSize(p_text);
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, min.y + ImFloor((max.y - min.y - size.y) * 0.5f)), ui.color.text_dim, p_text);
    x += size.x + ui.header.item_gap;
}

}
