#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

namespace lumen {

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

float TabStrip::dock_min_width(bool p_closable)
{
    const UiStyle& ui = ui_style();
    return ui.tab_strip.min_width + (p_closable ? close_size() : 0.0f) + ui.dock_tab.gap;
}

void TabStrip::fit(const ImVector<float>& p_natural, const ImVector<float>& p_min)
{
    min_factor = 1.0f;
    float total_natural = 0.0f;
    float total_min = 0.0f;
    for (int i = 0; i < p_natural.Size; ++i) {
        total_natural += p_natural[i];
        total_min += p_min[i];
    }
    if (total_natural <= avail) {
        scale = 1.0f;
        return;
    }
    if (total_min >= avail) {
        scale = 0.0f;
        min_factor = avail / ImMax(1.0f, total_min);
        return;
    }
    scale = avail / total_natural;
    for (int iter = 0; iter < p_natural.Size; ++iter) {
        float pinned = 0.0f;
        float free = 0.0f;
        for (int i = 0; i < p_natural.Size; ++i) {
            if (p_natural[i] * scale < p_min[i]) pinned += p_min[i];
            else free += p_natural[i];
        }
        const float next = free > 0.0f ? (avail - pinned) / free : scale;
        if (ImAbs(next - scale) < 1e-5f) break;
        scale = next;
    }
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
    min_factor = 1.0f;

    draw_list->AddRectFilled(min, max, background, rounding, round_flags);

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
        draw_list->AddRectFilled(a, b, ui.color.tab_active);
        if (accent_h > 0.0f) {
            const float ay = (edge == Edge::TOP) ? a.y : b.y - accent_h;
            draw_list->AddRectFilled(ImVec2(a.x, ay), ImVec2(b.x, ay + accent_h), ui.color.accent);
        }
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

    if (p_on) draw_list->AddRectFilled(a, b, ui.color.tab_active);
    else if (hovered) draw_list->AddRectFilled(a, b, ui.color.hover);
    if (p_on && ui.tab_strip.accent_height > 0.0f) {
        const float accent_h = ui.tab_strip.accent_height;
        const float ay = (edge == Edge::TOP) ? a.y : b.y - accent_h;
        draw_list->AddRectFilled(ImVec2(a.x, ay), ImVec2(b.x, ay + accent_h), ui.color.accent);
    }

    _draw_label(a, b, p_label, p_on || hovered);
    return pressed;
}

void TabStrip::text(const char* p_label)
{
    const float w = ImMax(ui_style().tab_strip.min_width, ImFloor(natural_width(p_label) * scale));
    const ImVec2 a(cursor_x, min.y);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w;
    _draw_label(a, b, p_label, false);
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

bool TabStrip::dock_tab(const char* p_label, bool p_selected, bool* p_close, bool p_dirty, ImU32 p_icon_color)
{
    const UiStyle& ui = ui_style();
    const bool closable = p_close != nullptr;
    const bool slot = closable || p_dirty;
    const float cs = slot ? close_size() : 0.0f;
    const float w = ImMax(1.0f, ImMax(ImFloor(dock_min_width(slot) * min_factor), ImFloor(dock_natural_width(p_label, slot) * scale)) - ui.dock_tab.gap);
    const ImVec2 a(cursor_x, min.y + ui.dock_tab.inset_top);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w + ui.dock_tab.gap;

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

    if (p_selected) draw_list->AddRectFilled(a, b, ui.color.tab_active, ui.dock_tab.rounding, ImDrawFlags_RoundCornersTop);
    else if (hot) draw_list->AddRectFilled(a, b, ui.color.tab_hover, ui.dock_tab.rounding, ImDrawFlags_RoundCornersTop);

    const float h = b.y - a.y;
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float pad = ImMax(ui.dock_tab.label_min_pad, ImFloor(ui.tab_strip.pad_x * scale));
    ImVec2 tmin(a.x + pad, ImFloor(a.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(slot ? ca.x - ui.dock_tab.gap : b.x - pad * 0.5f, b.y);
    const char* text = p_label;
    if (p_icon_color && end - p_label >= 3 && ((uint8_t)p_label[0] == 0xEE || (uint8_t)p_label[0] == 0xEF)) {
        text = p_label + 3;
        draw_list->AddText(tmin, p_icon_color, p_label, text);
        tmin.x += ImGui::CalcTextSize(p_label, text).x;
    }
    const ImVec2 text_size = ImGui::CalcTextSize(text, end);
    ImGui::PushStyleColor(ImGuiCol_Text, p_selected ? ui.color.text_hi : (hot ? ui.color.text : ui.color.text_dim));
    ImGui::RenderTextEllipsis(draw_list, tmin, tmax, tmax.x, text, end, &text_size);
    ImGui::PopStyleColor();

    const bool close_hot = close_hovered || close_held;
    const ImVec2 c(ca.x + cs * 0.5f, ca.y + cs * 0.5f);
    const float r = ImFloor(cs * ui.dock_tab.close_glyph_scale) + 0.5f;
    if (p_dirty && !close_hot) {
        draw_list->AddCircleFilled(c, r, (p_selected || hot) ? ui.color.text : ui.color.text_dim);
    } else if (closable && (p_selected || hot)) {
        if (close_hot) draw_list->AddRectFilled(ca, cb, ui.color.field, ui.dock_tab.close_rounding);
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

    ui_text_centered(draw_list, a, ImVec2(a.x + h, a.y + h), hovered ? ui.color.text : ui.color.text_dim, p_icon);
    return pressed;
}

bool TabStrip::console_field(const char* p_id, const char* p_hint, char* p_buf, size_t p_size, float p_field_width)
{
    const UiStyle& ui = ui_style();
    const float h = max.y - min.y;
    const float x = cursor_x + ui.console_bar.pad;

    ImGui::SetCursorScreenPos(ImVec2(x, ImFloor(min.y + (h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.console_bar.rounding);
    ui_field_push_style();
    ImGui::SetNextItemWidth(p_field_width);
    const bool submitted = ImGui::InputTextWithHint(p_id, p_hint, p_buf, p_size, ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted) ImGui::SetKeyboardFocusHere(-1);
    ui_field_pop_style();
    ImGui::PopStyleVar();

    cursor_x = x + p_field_width + ui.console_bar.pad;
    return submitted;
}

void TabStrip::end()
{
    ImGui::PopClipRect();
    ImGui::PopID();
}

}
