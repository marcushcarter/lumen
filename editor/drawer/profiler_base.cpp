#include <editor/drawer/profiler_base.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/imgui/imgui_helpers.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lumen {

/*****************/
/**** TOOLBAR ****/
/*****************/

void ProfilerTabBase::draw(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();

    if (ImGui::Button(frozen ? ICON_FA_PLAY "  Resume" : ICON_FA_PAUSE "  Freeze")) frozen = !frozen;
    _draw_toolbar(ctx);
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        imgui_title("Controls");
        imgui_property_row("Zoom", "%s", "Mouse Scroll");
        imgui_property_row("Zoom Area", "%s", "Mouse Drag");
        imgui_property_row("Zoom Out", "%s", "Double Click");
        imgui_property_row("Pan", "%s", "ALT + Mouse / Middle Mouse");
        imgui_property_row("Pan Horizontal", "%s", "SHIFT + Mouse Scroll");
        imgui_property_row("Scroll Lanes", "%s", "CTRL + Mouse Scroll");
        imgui_property_row("Freeze", "%s", "Space");
        imgui_property_row("Resume", "%s", "ESC");
        ImGui::EndTooltip();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.chrome);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("Graph", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) frozen = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) frozen = false;
    }

    _draw_graph(ctx);

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void ProfilerTabBase::_draw_empty(const char* p_text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size = ImGui::CalcTextSize(p_text);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - size.x) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - size.y) * 0.5f);
    ImGui::TextDisabled("%s", p_text);
}

/****************/
/**** CANVAS ****/
/****************/

void ProfilerTabBase::_canvas_begin(const char* p_id, float p_total_ms, float p_content_h)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float ruler_h = ImGui::GetTextLineHeight();

    total_ms = std::max(p_total_ms, MIN_RANGE_MS);
    canvas_w = std::max(avail.x, 1.0f);
    height = std::max(avail.y, p_content_h + ruler_h);
    dl = ImGui::GetWindowDrawList();
    origin = ImGui::GetCursorScreenPos();

    float range_ms = std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
    visible_start = std::clamp(visible_start, 0.0f, std::max(0.0f, total_ms - range_ms));
    px = canvas_w / range_ms;

    ImGui::InvisibleButton(p_id, ImVec2(canvas_w, height), ImGuiButtonFlags_MouseButtonLeft);
    hovered = ImGui::IsItemHovered();
    view_top = std::max(origin.y, ImGui::GetWindowPos().y);
    view_bottom = std::min(origin.y + height, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);

    dl->PushClipRect(origin, ImVec2(origin.x + canvas_w, origin.y + height), true);

    _canvas_input(range_ms);

    range_ms = std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
    visible_start = std::clamp(visible_start, 0.0f, std::max(0.0f, total_ms - range_ms));
    px = canvas_w / range_ms;
    dragging = panning || ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle);

    _canvas_grid();
}

void ProfilerTabBase::_canvas_input(float p_range_ms)
{
    const ImGuiIO& io = ImGui::GetIO();
    const UiStyle& ui = ui_style();

    if (hovered && io.MouseWheel != 0.0f && io.KeyCtrl) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * (ui.profiler.lane_header_height + ui.profiler.zone_height) * 3.0f);
    } else if (hovered && io.MouseWheel != 0.0f) {
        const float cursor_ms = visible_start + (io.MousePos.x - origin.x) / px;
        const float cursor_frac = (io.MousePos.x - origin.x) / canvas_w;
        const float new_range = std::clamp(p_range_ms * powf(0.85f, io.MouseWheel), MIN_RANGE_MS, total_ms);
        visible_start = cursor_ms - cursor_frac * new_range;
        visible_range = new_range;
    }

    if (hovered && io.MouseWheelH != 0.0f && visible_range > 0.0f) visible_start -= io.MouseWheelH * p_range_ms * 0.1f;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        _view_reset();
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (io.KeyAlt) {
            panning = true;
        } else {
            selecting = true;
            select_start_ms = visible_start + (io.MousePos.x - origin.x) / px;
            select_end_ms = select_start_ms;
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;

    if (panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) visible_start -= io.MouseDelta.x / px;
        else panning = false;
    }

    if (selecting) {
        select_end_ms = std::clamp(visible_start + (io.MousePos.x - origin.x) / px, 0.0f, total_ms);
        select_start_ms = std::clamp(select_start_ms, 0.0f, total_ms);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selecting = false;
            const float a = std::min(select_start_ms, select_end_ms);
            const float b = std::max(select_start_ms, select_end_ms);
            if (b - a > 0.01f) {
                visible_start = a;
                visible_range = b - a;
            }
        }
    }
}

void ProfilerTabBase::_canvas_grid()
{
    const UiStyle& ui = ui_style();
    const float visible_ms = canvas_w / px;
    const float block_ms = visible_ms >= 1.5f ? 1.0f : visible_ms >= 0.15f ? 0.1f : 0.01f;
    const float end_ms = _visible_end();
    const float ruler_h = ImGui::GetTextLineHeight();

    for (int i = (int)floorf(visible_start / block_ms); ; i++) {
        const float t = (float)i * block_ms;
        if (t > end_ms) break;
        const float x0 = _x_of(t);
        const float x1 = _x_of(t + block_ms);
        dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), (i & 1) == 0 ? ui.color.viz_stripe_even : ui.color.viz_stripe_odd);

        char text[32];
        if (block_ms == 1.0f) snprintf(text, sizeof(text), "%.0f ms", t);
        else if (block_ms == 0.1f) snprintf(text, sizeof(text), "%.1f ms", t);
        else snprintf(text, sizeof(text), "%.2f ms", t);
        dl->AddText(ImVec2(x0 + ui.profiler.ruler_text_pad, view_bottom - ruler_h), ui.color.viz_label, text);
    }
}

void ProfilerTabBase::_canvas_end(const char* p_status)
{
    const UiStyle& ui = ui_style();
    const ImGuiIO& io = ImGui::GetIO();

    if (selecting) {
        const float a = std::min(select_start_ms, select_end_ms);
        const float b = std::max(select_start_ms, select_end_ms);
        dl->AddRectFilled(ImVec2(_x_of(a), origin.y), ImVec2(_x_of(b), origin.y + height), ui.color.selection);
    }

    if (hovered) dl->AddLine(ImVec2(io.MousePos.x, origin.y), ImVec2(io.MousePos.x, origin.y + height), ui.color.viz_cursor, ui.profiler.cursor_stroke);

    if (p_status && p_status[0]) {
        const ImVec2 ts = ImGui::CalcTextSize(p_status);
        const ImVec2 sp = ui.profiler.status_pad;
        const float tx = origin.x + ui.profiler.label_pad;
        const float ty = view_bottom - ts.y - sp.x - ImGui::GetTextLineHeight();
        dl->AddRectFilled(ImVec2(tx - sp.x, ty - sp.y), ImVec2(tx + ts.x + sp.x, ty + ts.y + sp.y), ui.color.overlay_label);
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), p_status);
    }

    dl->PopClipRect();
}

/**************/
/**** VIEW ****/
/**************/

void ProfilerTabBase::_view_reset()
{
    visible_range = -1.0f;
    visible_start = 0.0f;
    selecting = false;
    panning = false;
}

float ProfilerTabBase::_visible_end() const
{
    return visible_start + std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
}

/**************/
/**** BARS ****/
/**************/

ImVec2 ProfilerTabBase::_bar(float p_ms, float p_len_ms, float p_y0, float p_y1, ImU32 p_fill, bool* r_hovered)
{
    const UiStyle& ui = ui_style();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 a(_x_of(p_ms) + ui.profiler.bar_inset, p_y0);
    const ImVec2 b(_x_of(p_ms + p_len_ms) - ui.profiler.bar_inset, p_y1);
    const bool in_canvas = io.MousePos.x >= origin.x && io.MousePos.x <= origin.x + canvas_w && io.MousePos.y >= view_top && io.MousePos.y <= view_bottom;
    const bool is_hovered = in_canvas && !dragging && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
    if (r_hovered) *r_hovered = is_hovered;
    if (b.x > a.x) dl->AddRectFilled(a, b, (r_hovered && is_hovered) ? ui.color.viz_hovered : p_fill, ui.profiler.bar_rounding);
    return ImVec2(a.x, b.x);
}

void ProfilerTabBase::_label(ImVec2 p_a, ImVec2 p_b, const char* p_text, ImU32 p_color)
{
    if (!p_text || !p_text[0]) return;
    const UiStyle& ui = ui_style();
    if (p_b.x - p_a.x < ui.profiler.label_min_width) return;
    const float lp = ui.profiler.label_pad;
    const ImVec2 ts = ImGui::CalcTextSize(p_text);
    const float x = std::max(p_a.x + lp, origin.x + lp);
    dl->PushClipRect(p_a, p_b, true);
    const ImVec2 p(x, p_a.y + ((p_b.y - p_a.y) - ts.y) * 0.5f);
    dl->AddText(ImVec2(p.x + ui.stroke.hairline, p.y + ui.stroke.hairline), ui.color.shadow_text, p_text);
    dl->AddText(p, p_color, p_text);
    dl->PopClipRect();
}

void ProfilerTabBase::_marker(float p_ms, float p_y0, float p_y1, ImU32 p_color)
{
    const float x = _x_of(p_ms);
    if (x < origin.x || x > origin.x + canvas_w) return;
    dl->AddLine(ImVec2(x, p_y0), ImVec2(x, p_y1), p_color, ui_style().profiler.marker_stroke);
}

}
