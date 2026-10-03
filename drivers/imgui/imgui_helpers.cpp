#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/pass_category.h>
#include <imgui_internal.h>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <windows.h>
#include <shellapi.h>

namespace lumen {

void imgui_title(const char* p_fmt, ...)
{
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextColoredV(ImGui::GetStyleColorVec4(ImGuiCol_TextLink), p_fmt, args);
    va_end(args);
}

void imgui_link(const char* label, const char* url)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.5f, 1.0f, 1.0f));
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#ifdef _WIN32
            ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
    }
    ImGui::PopStyleColor();
}

void imgui_spacing()
{
    ImGui::Spacing();
    ImGui::Spacing();
}

void imgui_section_gap() {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
}

void imgui_property_row(const char* p_name, const char* p_fmt, ...)
{
    constexpr float tab_width = 200.0f;
    ImGui::TextUnformatted(p_name);
    ImGui::SameLine(tab_width);
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextV(p_fmt, args);
    va_end(args);
}

void imgui_property_row_value_aligned(const char* p_name, const char* p_fmt, ...)
{
    ImGui::TextUnformatted(p_name);

    char buffer[256];

    va_list args;
    va_start(args, p_fmt);
    vsnprintf(buffer, sizeof(buffer), p_fmt, args);
    va_end(args);

    float value_width = ImGui::CalcTextSize(buffer).x;
    float right_edge = ImGui::GetContentRegionMax().x;

    ImGui::SameLine();
    ImGui::SetCursorPosX(right_edge - value_width);
    ImGui::TextUnformatted(buffer);
}

void imgui_cell_right(const char* p_text)
{
    float w = ImGui::CalcTextSize(p_text).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w));
    ImGui::TextUnformatted(p_text);
}

void imgui_cell_right_fmt(const char* p_fmt, ...)
{
    char buf[64];
    va_list ap; va_start(ap, p_fmt);
    vsnprintf(buf, sizeof(buf), p_fmt, ap);
    va_end(ap);
    imgui_cell_right(buf);
}

void imgui_tri_right(ImU32 p_color)
{
    ImVec2 c = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFontSize();
    float s = h * 0.42f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddTriangleFilled(ImVec2(c.x, c.y + h * 0.28f), ImVec2(c.x, c.y + h * 0.72f), ImVec2(c.x + s, c.y + h * 0.50f), p_color);
    ImGui::Dummy(ImVec2(s + 4.0f, h));
    ImGui::SameLine(0, 4.0f);
}

ImU32 imgui_rg_category_u32(const char* cat, float alpha)
{
    ImVec4 c(0.70f, 0.70f, 0.70f, alpha);
    if (cat && cat[0]) {
        if (const PassCategory* pc = pass_category_find(cat)) {
            c = ImVec4(pc->r / 255.0f, pc->g / 255.0f, pc->b / 255.0f, alpha);
        } else {
            uint64_t h = 1469598103934665603ull;
            for (const char* p = cat; *p; ++p) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
            c = (ImVec4)ImColor::HSV((float)(h % 360) / 360.0f, 0.55f, 0.95f);
            c.w = alpha;
        }
    }
    return ImGui::GetColorU32(c);
}

ImU32 imgui_pct_col(float pct)
{
    if (pct < 0.75f) return IM_COL32( 88, 180, 120, 255);
    if (pct < 0.90f) return IM_COL32(210, 170,  70, 255);
    return IM_COL32(210,  90,  80, 255);
}

SplitterState imgui_splitter(const char* id, SplitAxis axis, ImVec2 size, float grip_len)
{
    ImGui::InvisibleButton(id, size);
    SplitterState s;
    s.hovered = ImGui::IsItemHovered();
    s.active = ImGui::IsItemActive();
    s.activated = ImGui::IsItemActivated();

    if (s.hovered || s.active) ImGui::SetMouseCursor(axis == SplitAxis::X ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (s.active) s.delta = (axis == SplitAxis::X) ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;

    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    float cx = ImFloor((mn.x + mx.x) * 0.5f);
    float cy = ImFloor((mn.y + mx.y) * 0.5f);
    ImU32 col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : s.hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    const float t = 2.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (axis == SplitAxis::X) dl->AddRectFilled(ImVec2(cx - t, cy - grip_len * 0.5f), ImVec2(cx + t, cy + grip_len * 0.5f), col, t);
    else dl->AddRectFilled(ImVec2(cx - grip_len * 0.5f, cy - t), ImVec2(cx + grip_len * 0.5f, cy + t), col, t);
    
    return s;
}

}