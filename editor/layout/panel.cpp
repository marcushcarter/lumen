#include <editor/layout/panel.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <cstring>
#include <cstdio>

namespace lumen {

float panel_header_height()
{
    return ui_style().panel.header_height;
}

PanelTabResult panel_tab_bar(const PanelTab* p_tabs, int p_count, ImVec2 p_min, ImVec2 p_max, bool p_menu)
{
    const UiStyle& ui = ui_style();
    PanelTabResult result;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = p_max.y - p_min.y;
    const float r = ui.panel_box.rounding;
    dl->AddRectFilled(p_min, p_max, ui.color.header, r, ImDrawFlags_RoundCornersTop);

    const float pad = ui.panel.header_pad_x;
    const float menu_w = p_menu ? h : 0.0f;
    const float tabs_x1 = p_max.x - menu_w;
    const float icon_w = ImGui::GetFontSize() * 1.2f;
    const float icon_gap = ui.space.md;
    const float text_y = ImFloor(p_min.y + (h - ImGui::GetFontSize()) * 0.5f);

    if (p_count == 1) {
        const PanelTab& t = p_tabs[0];
        float x = p_min.x + pad;
        dl->PushClipRect(p_min, ImVec2(tabs_x1, p_max.y), true);
        if (t.icon) {
            const ImVec2 is = ImGui::CalcTextSize(t.icon);
            dl->AddText(ImVec2(ImFloor(x + (icon_w - is.x) * 0.5f), text_y), ui.color.text, t.icon);
            x += icon_w + icon_gap;
        }
        dl->AddText(ImVec2(x, text_y), ui.color.text, t.name);
        dl->PopClipRect();
    } else {
        const float btn_pad = ui.space.md;
        const float min_label = ImGui::CalcTextSize("W...").x;
        float natural = 0.0f;
        for (int i = 0; i < p_count; i++) natural += btn_pad * 2.0f + (p_tabs[i].icon ? icon_w + icon_gap : 0.0f) + ImGui::CalcTextSize(p_tabs[i].name).x + (p_tabs[i].dirty ? ImGui::CalcTextSize("*").x : 0.0f) + (p_tabs[i].closable ? p_max.y - p_min.y - btn_pad : 0.0f);
        const float avail = ImMax(1.0f, tabs_x1 - p_min.x);
        const float even = avail / (float)p_count;
        float x = p_min.x;
        for (int i = 0; i < p_count; i++) {
            const PanelTab& t = p_tabs[i];
            const float close_w = t.closable ? p_max.y - p_min.y - btn_pad : 0.0f;
            const float own = btn_pad * 2.0f + (t.icon ? icon_w + icon_gap : 0.0f) + ImGui::CalcTextSize(t.name).x + (t.dirty ? ImGui::CalcTextSize("*").x : 0.0f) + close_w;
            const float w = natural <= avail ? own : ImMax(icon_w + btn_pad * 2.0f, ImFloor(ImMin(own, even)));
            const ImVec2 a(x, p_min.y);
            const ImVec2 b(ImMin(x + w, tabs_x1), p_max.y);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(a);
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton("##tab", ImVec2(ImMax(1.0f, b.x - a.x), ImMax(1.0f, b.y - a.y)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle)) {
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) { if (t.closable) result.closed = i; }
                else result.clicked = i;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (t.active) dl->AddRectFilled(a, b, ui.color.toolbar);
            else if (hovered) dl->AddRectFilled(a, b, ui.color.tool_hover);
            float label_x1 = b.x - btn_pad;
            if (t.closable) {
                const float cs = b.y - a.y;
                const ImVec2 ca(b.x - cs, a.y);
                const ImVec2 cb(b.x, b.y);
                label_x1 = ca.x;
                ImGui::SetCursorScreenPos(ca);
                if (ImGui::InvisibleButton("##close", ImVec2(cs, cs))) result.closed = i;
                const bool close_hot = ImGui::IsItemHovered();
                if (close_hot) dl->AddRectFilled(ca, cb, t.active ? ui.color.tool_hover : ui.color.toolbar);
                if (hovered || close_hot || t.active) ui_text_centered(dl, ca, cb, ui.color.text, ICON_FA_XMARK);
            }
            if (hovered && w < own) ImGui::SetTooltip("%s", t.name);
            ImGui::PopID();

            float cx = a.x + btn_pad;
            dl->PushClipRect(a, b, true);
            if (t.icon) {
                const ImVec2 is = ImGui::CalcTextSize(t.icon);
                dl->AddText(ImVec2(ImFloor(cx + (icon_w - is.x) * 0.5f), text_y), ui.color.text, t.icon);
                cx += icon_w + icon_gap;
            }
            if (label_x1 - cx >= min_label || !t.icon) {
                char label[256];
                std::snprintf(label, sizeof(label), "%s%s", t.name, t.dirty ? "*" : "");
                ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
                ImGui::RenderTextEllipsis(dl, ImVec2(cx, text_y), ImVec2(label_x1, p_max.y), label_x1, label, nullptr, nullptr);
                ImGui::PopStyleColor();
            }
            dl->PopClipRect();
            x = b.x;
        }
    }

    if (p_menu) {
        const ImVec2 a(tabs_x1, p_min.y);
        ImGui::SetCursorScreenPos(a);
        result.menu = ImGui::InvisibleButton("##panel_menu", ImVec2(menu_w, h));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) dl->AddRectFilled(a, p_max, ui.color.tool_hover);
        ui_text_centered(dl, a, p_max, ui.color.text, ICON_FA_BARS);
    }
    return result;
}

void panel_header(const char* p_title, const char* p_icon, ImVec2 p_min, ImVec2 p_max)
{
    PanelTab tab;
    tab.name = p_title;
    tab.icon = p_icon;
    tab.active = true;
    panel_tab_bar(&tab, 1, p_min, p_max, false);
}

void panel_body(EditorContext& ctx, Panel& r_panel, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const ImVec2 size(ImMax(1.0f, p_max.x - p_min.x), ImMax(1.0f, p_max.y - p_min.y));
    ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ui.color.panel, ui.panel_box.rounding, ImDrawFlags_RoundCornersBottom);
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, r_panel.window_padding());
    ImGui::PushID(r_panel.name());
    const bool visible = ImGui::BeginChild("##content", size, ImGuiChildFlags_AlwaysUseWindowPadding, r_panel.window_flags());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (visible) {
        r_panel.draw_contents(ctx);
        ui_category_flush();
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void panel_draw(EditorContext& ctx, Panel& r_panel, ImVec2 p_min, ImVec2 p_max)
{
    const float header_h = panel_header_height();
    ImGui::PushID(r_panel.name());
    panel_header(r_panel.name(), r_panel.icon(), p_min, ImVec2(p_max.x, p_min.y + header_h));
    ImGui::PopID();
    panel_body(ctx, r_panel, ImVec2(p_min.x, p_min.y + header_h), p_max);
}

}
