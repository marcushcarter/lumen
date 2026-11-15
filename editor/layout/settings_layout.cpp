#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui_internal.h>

namespace lumen {

void SettingsLayout::draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar)
{
    const UiStyle& ui = ui_style();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = ImGui::GetContentRegionAvail();
    const float gap = ui.splitter.gap;
    const float bar_h = ui.toolbar.height;
    const float top = origin.y;
    const float body_h = ImMax(1.0f, origin.y + extent.y - top);
    const float nav_w = ImMin(ui.settings.nav_width, extent.x * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    _draw_nav(ImVec2(nav_w, body_h));

    const float cx = origin.x + nav_w + gap;
    const float cw = ImMax(1.0f, extent.x - nav_w - gap);
    ImGui::SetCursorScreenPos(ImVec2(cx, top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##settings_topbar", ImVec2(cw, bar_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (p_toolbar) p_toolbar(ctx);
    ImGui::EndChild();
    const float body_y = top + bar_h;
    const float content_h = ImMax(1.0f, origin.y + extent.y - body_y);

    if (pages.empty()) return;
    selected = ImClamp(selected, 0, (int)pages.size() - 1);

    ImGui::SetCursorScreenPos(ImVec2(cx, body_y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.settings.content_padding, ui.settings.content_padding));
    ImGui::BeginChild("##settings_content", ImVec2(cw, content_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const float width = ImMin(ImGui::GetContentRegionAvail().x, ui.settings.content_max_width);
    ImGui::PushItemWidth(width * (1.0f - ui.property.label_ratio));
    ImGui::BeginGroup();
    ui_title("%s", pages[selected].label.c_str());
    ui_section_gap();
    ui_field_push_style();
    pages[selected].draw(ctx);
    ui_field_pop_style();
    ImGui::EndGroup();
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

void SettingsLayout::_draw_nav(ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.space.lg, ui.space.lg));
    ImGui::BeginChild("##settings_nav", p_size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.base.frame_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.list_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.accent);
    for (int i = 0; i < (int)pages.size(); i++) {
        ImGui::PushID(i);
        if (ImGui::Selectable(pages[i].label.c_str(), i == selected, 0, ImVec2(0.0f, ui.settings.nav_item_height))) selected = i;
        ImGui::PopID();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::EndChild();
}

}
