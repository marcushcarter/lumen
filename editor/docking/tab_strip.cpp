#include <editor/docking/tab_strip.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

namespace lumen {

static ImU32 _mix_u32(const ImVec4& p_a, const ImVec4& p_b, float p_t)
{
    return ImGui::GetColorU32(ImVec4(p_a.x + (p_b.x - p_a.x) * p_t, p_a.y + (p_b.y - p_a.y) * p_t, p_a.z + (p_b.z - p_a.z) * p_t, 1.0f));
}

DockColors DockColors::get()
{
    const ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 bg = c[ImGuiCol_WindowBg];
    const ImVec4 fg = c[ImGuiCol_Text];

    DockColors d;
    d.gap = _mix_u32(bg, fg, 0.0f);
    d.strip = _mix_u32(bg, fg, 0.045f);
    d.pane = _mix_u32(bg, fg, 0.08f);
    d.tab_hovered = _mix_u32(bg, fg, 0.06f);
    d.line = _mix_u32(bg, fg, 0.12f);
    d.accent = ImGui::GetColorU32(ImGuiCol_TabSelectedOverline);
    d.text = ImGui::GetColorU32(ImGuiCol_Text);
    d.text_dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    d.drop_fill = ImGui::GetColorU32(ImGuiCol_DragDropTargetBg);
    d.drop_line = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    return d;
}

void dock_menu_push_style()
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

void dock_menu_pop_style()
{
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(9);
}

void dock_field_push_style()
{
    const ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 bg = c[ImGuiCol_WindowBg];
    const ImVec4 fg = c[ImGuiCol_Text];
    ImGui::PushStyleColor(ImGuiCol_FrameBg, _mix_u32(bg, fg, 0.11f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, _mix_u32(bg, fg, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, _mix_u32(bg, fg, 0.17f));
    ImGui::PushStyleColor(ImGuiCol_Header, _mix_u32(bg, fg, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, _mix_u32(bg, fg, 0.17f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, _mix_u32(bg, fg, 0.20f));
}

void dock_field_pop_style()
{
    ImGui::PopStyleColor(6);
}

bool dock_search_bar(const char* p_id, char* p_buf, size_t p_size, float p_width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    dock_field_push_style();
    ImGui::SetNextItemWidth(p_width);
    const bool changed = ImGui::InputTextWithHint(p_id, ICON_FA_MAGNIFYING_GLASS "  Search...", p_buf, p_size);
    dock_field_pop_style();
    ImGui::PopStyleVar();
    return changed;
}

float TabStrip::height()
{
    return ImFloor(ImGui::GetFontSize() + 10.0f);
}

float TabStrip::natural_width(const char* p_label)
{
    return ImGui::CalcTextSize(p_label, nullptr, true).x + PAD_X * 2.0f;
}

float TabStrip::close_size()
{
    return ImFloor(ImGui::GetFontSize() + 2.0f);
}

float TabStrip::dock_height()
{
    return height() + DOCK_INSET_TOP;
}

float TabStrip::dock_natural_width(const char* p_label)
{
    return ImGui::CalcTextSize(p_label, nullptr, true).x + ImGui::CalcTextSize(" ").x + PAD_X + close_size() + CLOSE_PAD + DOCK_TAB_GAP;
}

void TabStrip::begin(const char* p_id, ImVec2 p_min, ImVec2 p_max, Edge p_edge, float p_natural_total, float p_reserved_right, ImU32 p_bg)
{
    colors = DockColors::get();
    if (p_bg) colors.strip = p_bg;
    draw_list = ImGui::GetWindowDrawList();
    min = p_min;
    max = p_max;
    edge = p_edge;
    cursor_x = min.x;
    trailing_x = max.x;

    const float avail = ImMax(1.0f, (max.x - min.x) - p_reserved_right);
    scale = (p_natural_total > avail) ? avail / p_natural_total : 1.0f;

    draw_list->AddRectFilled(min, max, colors.strip);
    const float ly = (edge == Edge::TOP) ? max.y - 1.0f : min.y;
    draw_list->AddRectFilled(ImVec2(min.x, ly), ImVec2(max.x, ly + 1.0f), colors.line);

    ImGui::PushID(p_id);
    ImGui::PushClipRect(min, max, true);
}

bool TabStrip::tab(const char* p_label, bool p_selected)
{
    const float h = max.y - min.y;
    const float w = ImMax(MIN_TAB_W, ImFloor(natural_width(p_label) * scale));
    const ImVec2 a(cursor_x, min.y);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w;

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, h), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();

    if (p_selected) {
        draw_list->AddRectFilled(a, b, colors.pane);
        const float ay = (edge == Edge::TOP) ? a.y : b.y - ACCENT_H;
        draw_list->AddRectFilled(ImVec2(a.x, ay), ImVec2(b.x, ay + ACCENT_H), colors.accent);
    } else if (hovered) {
        const ImVec2 ha(a.x, edge == Edge::BOTTOM ? a.y + 1.0f : a.y);
        const ImVec2 hb(b.x, edge == Edge::TOP ? b.y - 1.0f : b.y);
        draw_list->AddRectFilled(ha, hb, colors.tab_hovered);
    }

    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float pad = ImMax(6.0f, ImFloor(PAD_X * scale));
    const ImVec2 tmin(a.x + pad, ImFloor(a.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(b.x - pad * 0.5f, b.y);
    ImGui::PushStyleColor(ImGuiCol_Text, (p_selected || hovered) ? colors.text : colors.text_dim);
    ImGui::RenderTextEllipsis(draw_list, tmin, tmax, tmax.x, p_label, end, &ts);
    ImGui::PopStyleColor();

    return pressed;
}

bool TabStrip::dock_tab(const char* p_label, bool p_selected, bool* p_close)
{
    const float cs = close_size();
    const float w = ImMax(MIN_TAB_W + cs, ImFloor(dock_natural_width(p_label) * scale) - DOCK_TAB_GAP);
    const ImVec2 a(cursor_x, min.y + DOCK_INSET_TOP);
    const ImVec2 b(cursor_x + w, max.y);
    cursor_x += w + DOCK_TAB_GAP;

    const ImVec2 ca(ImFloor(b.x - CLOSE_PAD * 0.5f - cs), ImFloor(a.y + (b.y - a.y - cs) * 0.5f));
    const ImVec2 cb(ca.x + cs, ca.y + cs);

    ImGui::PushID(p_label);
    ImGui::SetCursorScreenPos(ca);
    const bool close_pressed = ImGui::InvisibleButton("##close", ImVec2(cs, cs));
    const bool close_hovered = ImGui::IsItemHovered();
    const bool close_held = ImGui::IsItemActive();
    ImGui::PopID();

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(w, b.y - a.y), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered();
    const bool hot = hovered || close_hovered || close_held;

    if (p_selected) {
        draw_list->AddRectFilled(a, b, colors.pane, DOCK_ROUNDING, ImDrawFlags_RoundCornersTop);
    } else if (hot) {
        draw_list->AddRectFilled(a, ImVec2(b.x, b.y - 1.0f), colors.tab_hovered, DOCK_ROUNDING, ImDrawFlags_RoundCornersTop);
    }

    const float h = b.y - a.y;
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const float pad = ImMax(6.0f, ImFloor(PAD_X * scale));
    const ImVec2 tmin(a.x + pad, ImFloor(a.y + (h - ts.y) * 0.5f));
    const ImVec2 tmax(ca.x - 2.0f, b.y);
    ImGui::PushStyleColor(ImGuiCol_Text, (p_selected || hot) ? colors.text : colors.text_dim);
    ImGui::RenderTextEllipsis(draw_list, tmin, tmax, tmax.x, p_label, end, &ts);
    ImGui::PopStyleColor();

    if (p_selected || hot) {
        if (close_hovered || close_held) draw_list->AddRectFilled(ca, cb, colors.line, 3.0f);
        const ImVec2 c(ca.x + cs * 0.5f, ca.y + cs * 0.5f);
        const float r = ImFloor(cs * 0.22f) + 0.5f;
        const ImU32 xc = (close_hovered || close_held) ? colors.text : colors.text_dim;
        draw_list->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), xc, 1.5f);
        draw_list->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), xc, 1.5f);
    }

    if (p_close) *p_close = close_pressed || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
    return pressed;
}

bool TabStrip::trailing_button(const char* p_icon)
{
    const float h = max.y - min.y;
    trailing_x -= h;
    const ImVec2 a(trailing_x, min.y);
    const ImVec2 b(trailing_x + h, max.y);

    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton(p_icon, ImVec2(h, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        const ImVec2 ha(a.x, edge == Edge::BOTTOM ? a.y + 1.0f : a.y);
        const ImVec2 hb(b.x, edge == Edge::TOP ? b.y - 1.0f : b.y);
        draw_list->AddRectFilled(ha, hb, colors.tab_hovered);
    }

    const ImVec2 ts = ImGui::CalcTextSize(p_icon);
    draw_list->AddText(ImVec2(ImFloor(a.x + (h - ts.x) * 0.5f), ImFloor(a.y + (h - ts.y) * 0.5f)), hovered ? colors.text : colors.text_dim, p_icon);
    return pressed;
}

void TabStrip::end()
{
    ImGui::PopClipRect();
    ImGui::PopID();
}

}
