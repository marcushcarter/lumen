#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <core/rendering/pass_category.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <cstdarg>
#include <cfloat>
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
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

void imgui_section_gap()
{
    const float gap = ui_style().property.section_gap;
    ImGui::Dummy(ImVec2(0, gap));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, gap));
}

void imgui_property_row(const char* p_name, const char* p_fmt, ...)
{
    ImGui::TextUnformatted(p_name);
    ImGui::SameLine(ui_style().property.row_label_width);
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
    const float gap = ui_style().space.sm;
    ImGui::Dummy(ImVec2(s + gap, h));
    ImGui::SameLine(0, gap);
}

ImU32 imgui_rg_category_u32(const char* cat, float alpha)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(ui_style().color.viz_neutral);
    c.w = alpha;
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
    const UiStyle& ui = ui_style();
    if (pct < 0.75f) return ui.color.viz_ok;
    if (pct < 0.90f) return ui.color.viz_warn;
    return ui.color.viz_full;
}

SplitterState imgui_splitter(const char* id, SplitAxis axis, ImVec2 size, float grip_len)
{
    const UiStyle& ui = ui_style();
    if (grip_len < 0.0f) grip_len = ui.splitter.grip_length;
    ImGui::InvisibleButton(id, size);
    SplitterState s;
    s.hovered = ImGui::IsItemHovered() && ImGui::GetHoveredID() == ImGui::GetItemID();
    s.active = ImGui::IsItemActive();
    s.activated = ImGui::IsItemActivated();

    if (s.hovered || s.active) ImGui::SetMouseCursor(axis == SplitAxis::X ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (s.active) s.delta = (axis == SplitAxis::X) ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;

    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    float cx = ImFloor((mn.x + mx.x) * 0.5f);
    float cy = ImFloor((mn.y + mx.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (s.hovered || s.active) {
        ImU32 col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
        const float half = ui.splitter.line_width * 0.5f;
        if (axis == SplitAxis::X) dl->AddRectFilled(ImVec2(cx - half, mn.y), ImVec2(cx + half, mx.y), col);
        else dl->AddRectFilled(ImVec2(mn.x, cy - half), ImVec2(mx.x, cy + half), col);
    } else if (grip_len > 0.0f) {
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Separator);
        const float t = ui.splitter.grip_half_width;
        if (axis == SplitAxis::X) dl->AddRectFilled(ImVec2(cx - t, cy - grip_len * 0.5f), ImVec2(cx + t, cy + grip_len * 0.5f), col, t);
        else dl->AddRectFilled(ImVec2(cx - grip_len * 0.5f, cy - t), ImVec2(cx + grip_len * 0.5f, cy + t), col, t);
    }

    return s;
}

bool imgui_property_grid_begin(const char* p_id, float p_label_ratio)
{
    if (p_label_ratio < 0.0f) p_label_ratio = ui_style().property.label_ratio;
    if (!ImGui::BeginTable(p_id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, p_label_ratio);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - p_label_ratio);
    return true;
}

void imgui_property(const char* p_label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p_label);
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(p_label).x > ImGui::GetContentRegionAvail().x) ImGui::SetTooltip("%s", p_label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void imgui_property_grid_end()
{
    ImGui::EndTable();
}

bool imgui_category(const char* p_label, bool* r_remove)
{
    const UiStyle& ui = ui_style();
    const UiStyle::Details& d = ui.details;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    const float min_x = window->InnerRect.Min.x;
    const float max_x = window->InnerRect.Max.x;
    const float h = ImGui::GetFrameHeight() + d.header_pad_y * 2.0f;
    const float y0 = ImGui::GetCursorScreenPos().y;
    const float y1 = y0 + h;
    const float button = ImGui::GetFrameHeight();
    const float button_x = max_x - d.button_inset - button;

    ImGui::PushID(p_label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID open_id = ImGui::GetID("##open");
    bool open = storage->GetBool(open_id, true);

    const ImVec2 mouse = ImGui::GetMousePos();
    const bool in_row = ImGui::IsWindowHovered() && mouse.x >= min_x && mouse.x < max_x && mouse.y >= y0 && mouse.y < y1;
    const bool hovered = in_row && !(r_remove && mouse.x >= button_x);

    dl->PushClipRect(ImVec2(min_x, y0), ImVec2(max_x, y1), false);
    dl->AddRectFilled(ImVec2(min_x, y0), ImVec2(max_x, y1 - d.line), hovered ? ui.color.category_header_hover : ui.color.category_header);
    dl->AddRectFilled(ImVec2(min_x, y1 - d.line), ImVec2(max_x, y1), ui.color.category_line);
    const float text_y = y0 + (h - d.line - ImGui::GetFontSize()) * 0.5f;
    ImGui::RenderArrow(dl, ImVec2(min_x + d.arrow_x, text_y), ui.color.text, open ? ImGuiDir_Down : ImGuiDir_Right, d.arrow_scale);
    dl->AddText(ImVec2(min_x + d.header_indent, text_y), ui.color.text, p_label, ImGui::FindRenderedTextEnd(p_label));
    dl->PopClipRect();

    if (r_remove) {
        ImGui::SetCursorScreenPos(ImVec2(button_x, y0 + (h - d.line - button) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ui.color.transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.danger_soft);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.danger_soft_active);
        *r_remove = ImGui::Button(ICON_FA_TRASH_CAN "##remove", ImVec2(button, button));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
    }

    ImGui::SetCursorScreenPos(ImVec2(min_x, y0));
    if (ImGui::InvisibleButton("##header", ImVec2((r_remove ? button_x : max_x) - min_x, h))) {
        open = !open;
        storage->SetBool(open_id, open);
    }
    ImGui::PopID();
    return open;
}

bool imgui_drag_axes(const char* p_label, ImGuiDataType p_type, void* p_data, int p_count, float p_speed, const void* p_min, const void* p_max, const char* p_format, ImGuiSliderFlags p_flags)
{
    static const char* AXES[4] = { "X", "Y", "Z", "W" };
    const UiStyle& ui = ui_style();
    const UiStyle::Details& d = ui.details;
    const ImU32 colors[4] = { ui.color.axis_x, ui.color.axis_y, ui.color.axis_z, ui.color.axis_w };
    const size_t stride = ImGui::DataTypeGetInfo(p_type)->Size;
    p_count = ImClamp(p_count, 1, 4);

    const float h = ImGui::GetFrameHeight();
    const float part = ImMax(1.0f, ImFloor((ImGui::CalcItemWidth() - d.axis_gap * (float)(p_count - 1)) / (float)p_count));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID(p_label);
    for (int i = 0; i < p_count; i++) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + d.axis_tab_width, p.y + h), colors[i], ui.base.frame_rounding, ImDrawFlags_RoundCornersLeft);
        const ImVec2 t = ImGui::CalcTextSize(AXES[i]);
        dl->AddText(ImVec2(p.x + ImFloor((d.axis_tab_width - t.x) * 0.5f), p.y + ImFloor((h - t.y) * 0.5f)), ui.color.axis_text, AXES[i]);
        ImGui::Dummy(ImVec2(d.axis_tab_width, h));
        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushID(i);
        ImGui::SetNextItemWidth(ImMax(1.0f, part - d.axis_tab_width));
        changed |= ImGui::DragScalar("##v", p_type, (uint8_t*)p_data + stride * (size_t)i, p_speed, p_min, p_max, p_format, p_flags);
        ImGui::PopID();
        if (i + 1 < p_count) ImGui::SameLine(0.0f, d.axis_gap);
    }
    ImGui::PopID();

    const char* label_end = ImGui::FindRenderedTextEnd(p_label);
    if (label_end != p_label) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(p_label, label_end);
    }
    ImGui::EndGroup();
    return changed;
}

bool imgui_drag_float3_axes(const char* p_label, float* r_values, float p_speed, const char* p_format)
{
    return imgui_drag_axes(p_label, ImGuiDataType_Float, r_values, 3, p_speed, nullptr, nullptr, p_format);
}

}
