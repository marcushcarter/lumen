#include <drivers/imgui/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

namespace lumen {

void dock_menu_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ui.color.chrome);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.line);
    ImGui::PushStyleColor(ImGuiCol_Separator, ui.color.line);
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.panel);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.panel);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.panel);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.menu.padding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui.menu.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, ui.menu.border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.menu.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, ui.menu.rounding);
}

void dock_menu_pop_style()
{
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(9);
}

void dock_field_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.field);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.field_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.field_active);
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.field_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.field_active);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.field_pressed);
}

void dock_field_pop_style()
{
    ImGui::PopStyleColor(6);
}

bool dock_search_bar(const char* p_id, char* p_buf, size_t p_size, float p_width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui_style().radius.pill);
    dock_field_push_style();
    ImGui::SetNextItemWidth(p_width);
    const bool changed = ImGui::InputTextWithHint(p_id, ICON_FA_MAGNIFYING_GLASS "  Search...", p_buf, p_size);
    dock_field_pop_style();
    ImGui::PopStyleVar();
    return changed;
}

float TabStrip::height()
{
    return ImFloor(ImGui::GetFontSize() + ui_style().tab_strip.height_pad);
}

float TabStrip::natural_width(const char* p_label)
{
    return ImGui::CalcTextSize(p_label, nullptr, true).x + ui_style().tab_strip.pad_x * 2.0f;
}

float TabStrip::close_size()
{
    return ImFloor(ImGui::GetFontSize() + ui_style().dock_tab.close_size_pad);
}

float TabStrip::dock_height()
{
    return height() + ui_style().dock_tab.inset_top;
}

float TabStrip::dock_natural_width(const char* p_label, bool p_closable)
{
    const UiStyle& ui = ui_style();
    const float text = ImGui::CalcTextSize(p_label, nullptr, true).x;
    if (!p_closable) return text + ui.tab_strip.pad_x * 2.0f + ui.dock_tab.gap;
    return text + ImGui::CalcTextSize(ui.dock_tab.label_trail).x + ui.tab_strip.pad_x + close_size() + ui.dock_tab.close_pad + ui.dock_tab.gap;
}

void TabStrip::begin(const char* p_id, ImVec2 p_min, ImVec2 p_max, Edge p_edge, float p_natural_total, float p_reserved_right, ImU32 p_bg)
{
    const UiStyle& ui = ui_style();
    background = p_bg ? p_bg : ui.color.chrome;
    draw_list = ImGui::GetWindowDrawList();
    min = p_min;
    max = p_max;
    edge = p_edge;
    cursor_x = min.x;
    trailing_x = max.x;

    natural_total = p_natural_total;
    avail = ImMax(1.0f, (max.x - min.x) - p_reserved_right);
    scale = (p_natural_total > avail) ? avail / p_natural_total : 1.0f;
    scroll = nullptr;
    reveal = false;

    const float line = ui.stroke.hairline;
    draw_list->AddRectFilled(min, max, background);
    const float ly = (edge == Edge::TOP) ? max.y - line : min.y;
    draw_list->AddRectFilled(ImVec2(min.x, ly), ImVec2(max.x, ly + line), ui.color.line);

    ImGui::PushID(p_id);
    ImGui::PushClipRect(min, max, true);
}

bool TabStrip::tab(const char* p_label, bool p_selected)
{
    const UiStyle& ui = ui_style();
    const float h = max.y - min.y;
    const float w = ImMax(ui.tab_strip.min_width, ImFloor(natural_width(p_label) * scale));
    const ImVec2 a(cursor_x, min.y);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w;

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();

    const float line = ui.stroke.hairline;
    const float accent_h = ui.tab_strip.accent_height;
    if (p_selected) {
        draw_list->AddRectFilled(a, b, ui.color.panel);
        const float ay = (edge == Edge::TOP) ? a.y : b.y - accent_h;
        draw_list->AddRectFilled(ImVec2(a.x, ay), ImVec2(b.x, ay + accent_h), ui.color.accent);
    } else if (hovered) {
        const ImVec2 ha(a.x, edge == Edge::BOTTOM ? a.y + line : a.y);
        const ImVec2 hb(b.x, edge == Edge::TOP ? b.y - line : b.y);
        draw_list->AddRectFilled(ha, hb, ui.color.hover);
    }

    _draw_label(a, b, p_label, p_selected || hovered);
    return pressed;
}

bool TabStrip::button(const char* p_label, bool p_on)
{
    const UiStyle& ui = ui_style();
    const float h = max.y - min.y;
    const float w = ImMax(ui.tab_strip.min_width, ImFloor(natural_width(p_label) * scale));
    const ImVec2 a(cursor_x, min.y);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w;

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();

    const float line = ui.stroke.hairline;
    if (hovered) {
        const ImVec2 ha(a.x, edge == Edge::BOTTOM ? a.y + line : a.y);
        const ImVec2 hb(b.x, edge == Edge::TOP ? b.y - line : b.y);
        draw_list->AddRectFilled(ha, hb, ui.color.hover);
    }
    if (p_on) {
        const float accent_h = ui.tab_strip.accent_height;
        const float ay = (edge == Edge::TOP) ? a.y : b.y - accent_h;
        draw_list->AddRectFilled(ImVec2(a.x, ay), ImVec2(b.x, ay + accent_h), ui.color.accent);
    }

    _draw_label(a, b, p_label, p_on || hovered);
    return pressed;
}

void TabStrip::_draw_label(ImVec2 p_a, ImVec2 p_b, const char* p_label, bool p_bright)
{
    const UiStyle& ui = ui_style();
    const float h = p_b.y - p_a.y;
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float pad = ImMax(ui.dock_tab.label_min_pad, ImFloor(ui.tab_strip.pad_x * scale));
    const ImVec2 tmin(p_a.x + pad, ImFloor(p_a.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(p_b.x - pad * 0.5f, p_b.y);
    ImGui::PushStyleColor(ImGuiCol_Text, p_bright ? ui.color.text : ui.color.text_dim);
    ImGui::RenderTextEllipsis(draw_list, tmin, tmax, tmax.x, p_label, end, &ts);
    ImGui::PopStyleColor();
}

bool TabStrip::dock_tab(const char* p_label, bool p_selected, bool* p_close, bool p_dirty)
{
    const UiStyle& ui = ui_style();
    const bool closable = p_close != nullptr;
    const float cs = closable ? close_size() : 0.0f;
    const float w = ImMax(ui.tab_strip.min_width + cs, ImFloor(dock_natural_width(p_label, closable) * scale) - ui.dock_tab.gap);
    const ImVec2 a(cursor_x, min.y + ui.dock_tab.inset_top);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w + ui.dock_tab.gap;
    if (scroll && reveal && p_selected) {
        const float left = min.x + ui.dock_tab.inset_x;
        const float right = min.x + avail;
        if (a.x < left) *scroll -= left - a.x;
        else if (b.x > right) *scroll += b.x - right;
    }

    const ImVec2 ca(ImFloor(b.x - ui.dock_tab.close_pad * 0.5f - cs), ImFloor(a.y + (b.y - a.y - cs) * 0.5f));
    const ImVec2 cb(ca.x + cs, ca.y + cs);

    bool close_pressed = false;
    bool close_hovered = false;
    bool close_held = false;
    if (closable) {
        ImGui::PushID(p_label);
        ImGui::SetCursorScreenPos(ca);
        close_pressed = ImGui::InvisibleButton("##close", ImVec2(cs, cs));
        close_hovered = ImGui::IsItemHovered();
        close_held = ImGui::IsItemActive();
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, b.y - a.y), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();
    const bool hot = hovered || close_hovered || close_held;

    if (p_selected) {
        draw_list->AddRectFilled(a, b, ui.color.panel, ui.dock_tab.rounding, ImDrawFlags_RoundCornersTop);
    } else if (hot) {
        draw_list->AddRectFilled(a, ImVec2(b.x, b.y - ui.stroke.hairline), ui.color.hover, ui.dock_tab.rounding, ImDrawFlags_RoundCornersTop);
    }

    const float h = b.y - a.y;
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float pad = ImMax(ui.dock_tab.label_min_pad, ImFloor(ui.tab_strip.pad_x * scale));
    const ImVec2 tmin(a.x + pad, ImFloor(a.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(closable ? ca.x - ui.dock_tab.gap : b.x - pad * 0.5f, b.y);
    ImGui::PushStyleColor(ImGuiCol_Text, (p_selected || hot) ? ui.color.text : ui.color.text_dim);
    ImGui::RenderTextEllipsis(draw_list, tmin, tmax, tmax.x, p_label, end, &ts);
    ImGui::PopStyleColor();

    const bool close_hot = close_hovered || close_held;
    const ImVec2 c(ca.x + cs * 0.5f, ca.y + cs * 0.5f);
    const float r = ImFloor(cs * ui.dock_tab.close_glyph_scale) + 0.5f;
    if (closable && p_dirty && !close_hot) {
        draw_list->AddCircleFilled(c, r, (p_selected || hot) ? ui.color.text : ui.color.text_dim);
    } else if (closable && (p_selected || hot)) {
        if (close_hot) draw_list->AddRectFilled(ca, cb, ui.color.line, ui.dock_tab.close_rounding);
        const ImU32 xc = close_hot ? ui.color.text : ui.color.text_dim;
        draw_list->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), xc, ui.dock_tab.close_glyph_stroke);
        draw_list->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), xc, ui.dock_tab.close_glyph_stroke);
    }

    if (p_close) *p_close = close_pressed || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
    return pressed;
}

bool TabStrip::trailing_button(const char* p_icon)
{
    const UiStyle& ui = ui_style();
    const float h = max.y - min.y;
    trailing_x -= h;
    const ImVec2 a(trailing_x, min.y);
    const ImVec2 b(trailing_x + h, max.y);

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_icon, ImVec2(h, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        const float line = ui.stroke.hairline;
        const ImVec2 ha(a.x, edge == Edge::BOTTOM ? a.y + line : a.y);
        const ImVec2 hb(b.x, edge == Edge::TOP ? b.y - line : b.y);
        draw_list->AddRectFilled(ha, hb, ui.color.hover);
    }

    const ImVec2 ts = ImGui::CalcTextSize(p_icon);
    draw_list->AddText(ImVec2(ImFloor(a.x + (h - ts.x) * 0.5f), ImFloor(a.y + (h - ts.y) * 0.5f)), hovered ? ui.color.text : ui.color.text_dim, p_icon);
    return pressed;
}

bool TabStrip::console_field(const char* p_id, const char* p_label, const char* p_hint, char* p_buf, size_t p_size, float p_field_width)
{
    const UiStyle& ui = ui_style();
    const float h = max.y - min.y;
    float x = cursor_x + ui.console_bar.pad;

    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ls = ImGui::CalcTextSize(p_label, end);
    draw_list->AddText(ImVec2(x, ImFloor(min.y + (h - ls.y) * 0.5f)), ui.color.text_dim, p_label, end);
    x += ls.x + ui.console_bar.pad;

    ImGui::SetCursorScreenPos(ImVec2(x, ImFloor(min.y + (h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.console_bar.rounding);
    dock_field_push_style();
    ImGui::SetNextItemWidth(p_field_width);
    const bool submitted = ImGui::InputTextWithHint(p_id, p_hint, p_buf, p_size, ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted) ImGui::SetKeyboardFocusHere(-1);
    dock_field_pop_style();
    ImGui::PopStyleVar();

    cursor_x = x + p_field_width + ui.console_bar.pad;
    return submitted;
}

// Keeps tabs at their natural width; the wheel scrolls them sideways when they overflow.
void TabStrip::scrollable(float& r_scroll, bool p_reveal, float p_lead)
{
    scale = 1.0f;
    scroll = &r_scroll;
    reveal = p_reveal;
    const float overflow = ImMax(0.0f, natural_total + p_lead - avail);
    if (overflow > 0.0f && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max)) {
        const ImGuiIO& io = ImGui::GetIO();
        const float wheel = io.MouseWheelH != 0.0f ? -io.MouseWheelH : io.MouseWheel;
        r_scroll -= wheel * ImGui::GetFontSize() * 3.0f;
    }
    r_scroll = ImClamp(r_scroll, 0.0f, overflow);
    cursor_x -= r_scroll;
}

void TabStrip::end()
{
    ImGui::PopClipRect();
    ImGui::PopID();
}

}