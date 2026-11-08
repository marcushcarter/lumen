#include <editor/tabs/asset_manager/asset_manager_tab.h>
#include <imgui.h>

namespace lumen {

Error AssetManagerTab::initialize()
{
    layout.initialize("##AssetManagerHost");
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { _draw_browser(r_ctx, p_min, p_max); };
    return Error::OK;
}

void AssetManagerTab::shutdown()
{
    layout.shutdown();
}

void AssetManagerTab::_draw_browser(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const ImVec2 size(ImMax(p_max.x - p_min.x, 1.0f), ImMax(p_max.y - p_min.y, 1.0f));
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##asset_manager", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    manager.draw(ctx);
    ImGui::EndChild();
}

void AssetManagerTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, {});
}

}
