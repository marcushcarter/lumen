#include <editor/tabs/content/content_tab.h>
#include <editor/panels/content_browser_panel.h>
#include <editor/panels/shared_panels.h>
#include <imgui.h>

namespace lumen {

Error ContentTab::initialize(SharedPanels& r_shared)
{
    layout.initialize("##ContentHost");
    for (auto& p : r_shared.panels) {
        if (p.get() != r_shared.content_browser) layout.add_shared(p.get());
    }
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { _draw_browser(r_ctx, p_min, p_max); };
    return Error::OK;
}

void ContentTab::shutdown()
{
    layout.shutdown();
}

void ContentTab::_draw_browser(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    if (!ctx.shared_panels || !ctx.shared_panels->content_browser) return;
    const UiStyle& ui = ui_style();
    const ImVec2 size(ImMax(p_max.x - p_min.x, 1.0f), ImMax(p_max.y - p_min.y, 1.0f));
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.dock.drawer_padding);
    ImGui::BeginChild("##content_fullscreen", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ctx.shared_panels->content_browser->draw_contents(ctx);
    ImGui::EndChild();
}

void ContentTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, {});
}

}
