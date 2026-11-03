#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui_internal.h>

namespace lumen {

void SettingsLayout::draw(EditorContext& ctx, const char* p_host_id, const std::function<void(EditorContext&)>& p_toolbar)
{
    const UiStyle& ui = ui_style();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(p_host_id, nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar(3);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = ImGui::GetContentRegionAvail();
    const float bar_h = ui.toolbar.height;

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.toolbar.padding_x, ImFloor((bar_h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::BeginChild("##settings_topbar", ImVec2(extent.x, bar_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (p_toolbar) p_toolbar(ctx);
    ImGui::EndChild();

    const float body_y = origin.y + bar_h;
    const float body_h = ImMax(1.0f, origin.y + extent.y - body_y);
    const float nav_w = ImMin(ui.settings.nav_width, extent.x * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, body_y));
    _draw_nav(ImVec2(nav_w, body_h));

    if (pages.empty()) {
        ImGui::End();
        return;
    }
    selected = ImClamp(selected, 0, (int)pages.size() - 1);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + nav_w, body_y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.settings.content_padding, ui.settings.content_padding));
    ImGui::BeginChild("##settings_content", ImVec2(extent.x - nav_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding);
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

    ImGui::End();
}

void SettingsLayout::_draw_nav(ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.chrome);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.space.sm, ui.space.sm));
    ImGui::BeginChild("##settings_nav", p_size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    const ImVec2 win = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(win.x + p_size.x - ui.stroke.hairline, win.y), ImVec2(win.x + p_size.x, win.y + p_size.y), ui.color.line);

    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    for (int i = 0; i < (int)pages.size(); i++) {
        ImGui::PushID(i);
        if (ImGui::Selectable(pages[i].label.c_str(), i == selected, 0, ImVec2(0.0f, ui.settings.nav_item_height))) selected = i;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

}
