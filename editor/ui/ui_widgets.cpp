#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <core/base/hash.h>
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

void ui_menu_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ui.color.menu_back);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.menu_outline);
    ImGui::PushStyleColor(ImGuiCol_Separator, ui_hex_over(UiStyle::Palette::SEPARATOR, UiStyle::Palette::MENU_BACK));
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.field);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.field_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.field_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.menu_text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ui.color.menu_dim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.menu.padding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui.menu.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, ui.menu.border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.base.frame_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, ui.base.grab_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui.space.lg, ImFloor(ui.unit - ImGui::GetFontSize())));
}

void ui_menu_pop_style()
{
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(11);
}

void ui_context_menu_push_style()
{
    ui_menu_push_style();
}

void ui_context_menu_pop_style()
{
    ui_menu_pop_style();
}

void ui_field_push_style()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.text_field);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.text_field_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.text_field_active);
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.list_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.list_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.list_active);
}

void ui_field_pop_style()
{
    ImGui::PopStyleColor(6);
}

bool ui_search_bar(const char* p_id, char* p_buf, size_t p_size, float p_width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui_style().radius.xl);
    ui_field_push_style();
    ImGui::SetNextItemWidth(p_width);
    const bool changed = ImGui::InputTextWithHint(p_id, ICON_FA_MAGNIFYING_GLASS "  Search", p_buf, p_size);
    ui_field_pop_style();
    ImGui::PopStyleVar();
    return changed;
}

void ui_push_buttons(UiButton p_kind)
{
    const UiStyle& ui = ui_style();
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    switch (p_kind) {
        case UiButton::GHOST:
            ImGui::PushStyleColor(ImGuiCol_Button, ui.color.transparent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.transparent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.transparent);
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            break;
        case UiButton::DANGER:
            ImGui::PushStyleColor(ImGuiCol_Button, ui.color.danger_soft);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.danger_soft_active);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.danger_strong);
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            break;
        case UiButton::PRIMARY:
            ImGui::PushStyleColor(ImGuiCol_Button, ui.color.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.accent_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.accent_active);
            ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
            break;
        case UiButton::SUCCESS:
            ImGui::PushStyleColor(ImGuiCol_Button, ui.color.success_soft);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.success_soft_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.success_soft_active);
            ImGui::PushStyleColor(ImGuiCol_Text, ui.color.success_text);
            break;
        case UiButton::NEUTRAL:
            ImGui::PushStyleColor(ImGuiCol_Button, ui.color.field);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.field_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.field_active);
            ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
            break;
    }
}

void ui_pop_buttons()
{
    ImGui::PopStyleColor(4);
}

bool ui_icon_button(const char* p_icon, float p_size, const char* p_tooltip)
{
    const bool pressed = ImGui::Button(p_icon, ImVec2(p_size, p_size));
    if (p_tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_tooltip);
    return pressed;
}

UiRename ui_inline_rename(ImVec2 p_pos, float p_width, char* r_buf, size_t p_size, bool& r_focus)
{
    const float pad = ui_style().outliner.rename_pad_x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(p_pos.x - pad, p_pos.y));
    ImGui::SetNextItemWidth(p_width);
    if (r_focus) {
        ImGui::SetKeyboardFocusHere();
        r_focus = false;
    }
    const bool enter = ImGui::InputText("##rename", r_buf, p_size, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleVar();
    if (!enter && !ImGui::IsItemDeactivated()) return UiRename::NONE;
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) ? UiRename::CANCEL : UiRename::COMMIT;
}

void ui_title(const char* p_fmt, ...)
{
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextColoredV(ImGui::GetStyleColorVec4(ImGuiCol_TextLink), p_fmt, args);
    va_end(args);
}

void ui_text_centered(ImDrawList* p_dl, ImVec2 p_min, ImVec2 p_max, ImU32 p_color, const char* p_text, const char* p_end)
{
    const ImVec2 ts = ImGui::CalcTextSize(p_text, p_end);
    p_dl->AddText(ImVec2(ImFloor(p_min.x + (p_max.x - p_min.x - ts.x) * 0.5f), ImFloor(p_min.y + (p_max.y - p_min.y - ts.y) * 0.5f)), p_color, p_text, p_end);
}

void ui_open_url(const char* p_url)
{
    ShellExecuteA(nullptr, "open", p_url, nullptr, nullptr, SW_SHOWNORMAL);
}

void ui_link(const char* p_label, const char* p_url)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
    ImGui::TextUnformatted(p_label);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ui_open_url(p_url);
    }
    ImGui::PopStyleColor();
}

void ui_spacing()
{
    ImGui::Spacing();
    ImGui::Spacing();
}

void ui_section_gap()
{
    const float gap = ui_style().property.section_gap;
    ImGui::Dummy(ImVec2(0, gap));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, gap));
}

void ui_section_label(const char* p_label)
{
    ImGui::Dummy(ImVec2(0, ui_style().property.section_gap));
    ImGui::TextDisabled("%s", p_label);
}

void ui_warning(const char* p_fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, p_fmt);
    std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui_style().color.warning));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  %s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void ui_property_row(const char* p_name, const char* p_fmt, ...)
{
    ImGui::TextUnformatted(p_name);
    ImGui::SameLine(ui_style().property.row_label_width);
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextV(p_fmt, args);
    va_end(args);
}

void ui_property_row_value_aligned(const char* p_name, const char* p_fmt, ...)
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

void ui_cell_right(const char* p_text)
{
    float w = ImGui::CalcTextSize(p_text).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w));
    ImGui::TextUnformatted(p_text);
}

void ui_cell_right_fmt(const char* p_fmt, ...)
{
    char buf[64];
    va_list ap; va_start(ap, p_fmt);
    vsnprintf(buf, sizeof(buf), p_fmt, ap);
    va_end(ap);
    ui_cell_right(buf);
}

void ui_tri_right(ImU32 p_color)
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

ImU32 ui_category_color(const char* p_category, float p_alpha)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(ui_style().color.viz_neutral);
    c.w = p_alpha;
    if (p_category && p_category[0]) {
        if (const PassCategory* pc = pass_category_find(p_category)) {
            c = ImVec4(pc->r / 255.0f, pc->g / 255.0f, pc->b / 255.0f, p_alpha);
        } else {
            const uint64_t h = hash_fnv(p_category);
            c = (ImVec4)ImColor::HSV((float)(h % 360) / 360.0f, 0.55f, 0.95f);
            c.w = p_alpha;
        }
    }
    return ImGui::GetColorU32(c);
}

ImU32 ui_percent_color(float p_percent)
{
    const UiStyle& ui = ui_style();
    if (p_percent < 0.75f) return ui.color.viz_ok;
    if (p_percent < 0.90f) return ui.color.viz_warn;
    return ui.color.viz_full;
}

SplitterState ui_splitter(const char* p_id, SplitAxis p_axis, ImVec2 p_size, float p_grip_len)
{
    const UiStyle& ui = ui_style();
    if (p_grip_len < 0.0f) p_grip_len = ui.splitter.grip_length;
    ImGui::InvisibleButton(p_id, p_size);
    SplitterState s;
    s.hovered = ImGui::IsItemHovered() && ImGui::GetHoveredID() == ImGui::GetItemID();
    s.active = ImGui::IsItemActive();
    s.activated = ImGui::IsItemActivated();

    if (s.hovered || s.active) ImGui::SetMouseCursor(p_axis == SplitAxis::X ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (s.active) s.delta = (p_axis == SplitAxis::X) ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;

    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    float cx = ImFloor((mn.x + mx.x) * 0.5f);
    float cy = ImFloor((mn.y + mx.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (s.hovered || s.active) {
        ImU32 col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
        const float half = ui.splitter.line_width * 0.5f;
        if (p_axis == SplitAxis::X) dl->AddRectFilled(ImVec2(cx - half, mn.y), ImVec2(cx + half, mx.y), col);
        else dl->AddRectFilled(ImVec2(mn.x, cy - half), ImVec2(mx.x, cy + half), col);
    } else if (p_grip_len > 0.0f) {
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Separator);
        const float t = ui.splitter.grip_half_width;
        if (p_axis == SplitAxis::X) dl->AddRectFilled(ImVec2(cx - t, cy - p_grip_len * 0.5f), ImVec2(cx + t, cy + p_grip_len * 0.5f), col, t);
        else dl->AddRectFilled(ImVec2(cx - p_grip_len * 0.5f, cy - t), ImVec2(cx + p_grip_len * 0.5f, cy + t), col, t);
    }

    return s;
}

bool ui_property_grid_begin(const char* p_id, float p_label_ratio)
{
    if (p_label_ratio < 0.0f) p_label_ratio = ui_style().property.label_ratio;
    if (!ImGui::BeginTable(p_id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, p_label_ratio);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - p_label_ratio);
    return true;
}

void ui_property(const char* p_label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p_label);
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(p_label).x > ImGui::GetContentRegionAvail().x) ImGui::SetTooltip("%s", p_label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void ui_property_grid_end()
{
    ImGui::EndTable();
}

struct OpenPanel
{
    ImGuiWindow* window = nullptr;
    ImDrawListSplitter splitter;
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f;
    float inset_left = 0.0f;
    float inset_right = 0.0f;
    bool active = false;
};

static OpenPanel g_open_panel;

void ui_chevron(ImDrawList* p_dl, ImVec2 p_center, float p_size, ImGuiDir p_dir, ImU32 p_color, float p_thickness)
{
    const float h = p_size * 0.5f;
    const float q = p_size * 0.25f;
    ImVec2 pts[3];
    if (p_dir == ImGuiDir_Down) {
        pts[0] = ImVec2(p_center.x - h, p_center.y - q);
        pts[1] = ImVec2(p_center.x, p_center.y + q);
        pts[2] = ImVec2(p_center.x + h, p_center.y - q);
    } else if (p_dir == ImGuiDir_Up) {
        pts[0] = ImVec2(p_center.x - h, p_center.y + q);
        pts[1] = ImVec2(p_center.x, p_center.y - q);
        pts[2] = ImVec2(p_center.x + h, p_center.y + q);
    } else if (p_dir == ImGuiDir_Left) {
        pts[0] = ImVec2(p_center.x + q, p_center.y - h);
        pts[1] = ImVec2(p_center.x - q, p_center.y);
        pts[2] = ImVec2(p_center.x + q, p_center.y + h);
    } else {
        pts[0] = ImVec2(p_center.x - q, p_center.y - h);
        pts[1] = ImVec2(p_center.x + q, p_center.y);
        pts[2] = ImVec2(p_center.x - q, p_center.y + h);
    }
    p_dl->AddPolyline(pts, 3, p_color, ImDrawFlags_None, p_thickness);
}

static void _panel_grip(ImDrawList* p_dl, ImVec2 p_right_center, ImU32 p_color)
{
    const UiStyle::PanelBox& pb = ui_style().panel_box;
    const float s = pb.grip_spacing;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 4; col++) {
            const ImVec2 c(ImFloor(p_right_center.x - (3 - col) * s), ImFloor(p_right_center.y + (row == 0 ? -s * 0.5f : s * 0.5f)));
            p_dl->AddRectFilled(c, ImVec2(c.x + pb.grip_dot, c.y + pb.grip_dot), p_color);
        }
    }
}

void ui_category_flush()
{
    OpenPanel& op = g_open_panel;
    if (!op.active) return;
    op.active = false;
    const UiStyle& ui = ui_style();
    ImGuiWindow* window = op.window;
    ImDrawList* dl = window->DrawList;
    const bool current = ImGui::GetCurrentWindow() == window;

    if (current) ImGui::Unindent(op.inset_left);
    window->WorkRect.Max.x += op.inset_right;
    window->ContentRegionRect.Max.x += op.inset_right;

    const float y1 = window->DC.CursorPos.y - ImGui::GetStyle().ItemSpacing.y + ui.panel_box.body_pad_y;
    op.splitter.SetCurrentChannel(dl, 0);
    if (y1 > op.y0) dl->AddRectFilled(ImVec2(op.x0, op.y0), ImVec2(op.x1, y1), ui.color.category_header, ui.panel_box.rounding, ImDrawFlags_RoundCornersBottom);
    op.splitter.Merge(dl);

    if (current) {
        ImGui::SetCursorScreenPos(ImVec2(window->DC.CursorPos.x, ImMax(y1, window->DC.CursorPos.y)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
}

bool ui_category(const char* p_label, bool* r_remove)
{
    ui_category_flush();
    if (r_remove) *r_remove = false;
    const UiStyle& ui = ui_style();
    const UiStyle::Details& d = ui.details;
    const UiStyle::PanelBox& pb = ui.panel_box;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    ImGui::Dummy(ImVec2(0.0f, ImMax(0.0f, pb.gap - ImGui::GetStyle().ItemSpacing.y)));

    const float min_x = window->InnerRect.Min.x + pb.margin_x;
    const float max_x = window->InnerRect.Max.x - pb.margin_x;
    const float h = pb.header_height;
    const float y0 = ImGui::GetCursorScreenPos().y;
    const float y1 = y0 + h;
    const float button = ImGui::GetFrameHeight();
    const float grip_w = pb.grip_spacing * 4.0f + ui.space.md;
    const float button_x = max_x - grip_w - button;

    ImGui::PushID(p_label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID open_id = ImGui::GetID("##open");
    bool open = storage->GetBool(open_id, false);

    const ImVec2 mouse = ImGui::GetMousePos();
    const bool in_row = ImGui::IsWindowHovered() && mouse.x >= min_x && mouse.x < max_x && mouse.y >= y0 && mouse.y < y1;
    const bool hovered = in_row && !(r_remove && mouse.x >= button_x && mouse.x < button_x + button);

    const ImRect& clip = window->InnerClipRect;
    dl->PushClipRect(ImMax(ImVec2(min_x, y0), clip.Min), ImMin(ImVec2(max_x, y1), clip.Max), true);
    dl->AddRectFilled(ImVec2(min_x, y0), ImVec2(max_x, y1), hovered ? ui.color.category_header_hover : ui.color.category_header, pb.rounding, open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    const float cy = ImFloor(y0 + h * 0.5f);
    const float chev = ImFloor(ImGui::GetFontSize() * 0.5f);
    ui_chevron(dl, ImVec2(ImFloor(min_x + d.arrow_x + chev * 0.5f) + 0.5f, cy + 0.5f), chev, open ? ImGuiDir_Down : ImGuiDir_Right, ui.color.text, ui.stroke.thin);
    const float text_y = ImFloor(y0 + (h - ImGui::GetFontSize()) * 0.5f);
    dl->AddText(ImVec2(min_x + d.header_indent, text_y), ui.color.text, p_label, ImGui::FindRenderedTextEnd(p_label));
    _panel_grip(dl, ImVec2(max_x - ui.space.md, cy), ui_hex(0x6b6b6bff));
    dl->PopClipRect();

    if (r_remove) {
        ImGui::SetCursorScreenPos(ImVec2(button_x, y0 + (h - button) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ui.color.transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.danger_soft);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.danger_soft_active);
        ImGui::PushStyleColor(ImGuiCol_Border, ui.color.transparent);
        *r_remove = ImGui::Button(ICON_FA_XMARK "##remove", ImVec2(button, button));
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
    }

    ImGui::SetCursorScreenPos(ImVec2(min_x, y0));
    if (ImGui::InvisibleButton("##header", ImVec2((r_remove ? button_x : max_x) - min_x, h))) {
        open = !open;
        storage->SetBool(open_id, open);
    }
    ImGui::SetCursorScreenPos(ImVec2(window->DC.CursorPos.x, y1));
    ImGui::PopID();

    if (!open) return false;

    OpenPanel& op = g_open_panel;
    op.window = window;
    op.x0 = min_x;
    op.x1 = max_x;
    op.y0 = y1;
    op.inset_left = pb.margin_x + ui.space.lg;
    op.inset_right = pb.margin_x + ui.space.lg;
    op.active = true;
    op.splitter.Split(dl, 2);
    op.splitter.SetCurrentChannel(dl, 1);
    window->WorkRect.Max.x -= op.inset_right;
    window->ContentRegionRect.Max.x -= op.inset_right;
    ImGui::Indent(op.inset_left);
    ImGui::Dummy(ImVec2(0.0f, ImMax(0.0f, pb.body_pad_y - ImGui::GetStyle().ItemSpacing.y)));
    return true;
}

bool ui_drag_axes(const char* p_label, ImGuiDataType p_type, void* p_data, int p_count, float p_speed, const void* p_min, const void* p_max, const char* p_format, ImGuiSliderFlags p_flags)
{
    static const char* AXES[4] = { "X", "Y", "Z", "W" };
    const UiStyle& ui = ui_style();
    const size_t stride = ImGui::DataTypeGetInfo(p_type)->Size;
    p_count = ImClamp(p_count, 1, 4);

    const float h = ImGui::GetFrameHeight();
    const float avail = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float label_w = ImFloor(avail * ui.property.label_ratio);
    const float w = ImMax(1.0f, avail - label_w - gap);
    const float rounding = ui.base.frame_rounding;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float field_x = origin.x + label_w + gap;
    const char* label_end = ImGui::FindRenderedTextEnd(p_label);

    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID(p_label);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.transparent);
    for (int i = 0; i < p_count; i++) {
        const float row_y = origin.y + h * (float)i;
        char text[128];
        if (i == 0 && label_end != p_label) std::snprintf(text, sizeof(text), "%.*s %s", (int)(label_end - p_label), p_label, AXES[i]);
        else std::snprintf(text, sizeof(text), "%s", AXES[i]);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 tmin(ImMax(origin.x, origin.x + label_w - ts.x), row_y + ImFloor((h - ts.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
        ImGui::RenderTextEllipsis(dl, tmin, ImVec2(origin.x + label_w, row_y + h), origin.x + label_w, text, nullptr, &ts);
        ImGui::PopStyleColor();

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);
        ImGui::SetCursorScreenPos(ImVec2(field_x, row_y));
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(w);
        changed |= ImGui::DragScalar("##v", p_type, (uint8_t*)p_data + stride * (size_t)i, p_speed, p_min, p_max, p_format, p_flags);
        ImGui::PopID();
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();

        splitter.SetCurrentChannel(dl, 0);
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (p_count == 1) corners = ImDrawFlags_RoundCornersAll;
        else if (i == 0) corners = ImDrawFlags_RoundCornersTop;
        else if (i == p_count - 1) corners = ImDrawFlags_RoundCornersBottom;
        const ImU32 fill = active ? ui_hex(UiStyle::Palette::NUM_ACTIVE) : (hovered ? ui.color.field_hover : ui.color.field);
        dl->AddRectFilled(a, b, fill, rounding, corners);
        if (i > 0) dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, a.y + 1.0f), ui.color.outline);
        splitter.Merge(dl);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    dl->AddRect(ImVec2(field_x, origin.y), ImVec2(field_x + w, origin.y + h * (float)p_count), ui.color.outline, rounding);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h * (float)p_count));
    ImGui::Dummy(ImVec2(avail, ui.space.sm));
    ImGui::EndGroup();
    return changed;
}

bool ui_drag_float3_axes(const char* p_label, float* r_values, float p_speed, const char* p_format)
{
    return ui_drag_axes(p_label, ImGuiDataType_Float, r_values, 3, p_speed, nullptr, nullptr, p_format);
}

}
