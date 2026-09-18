#include <editor/popup/settings/project_settings.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <imgui.h>

namespace lumen {

void ProjectSettingsPopup::draw_contents(EditorContext& ctx)
{
    ImGui::DragInt("Window width", &ctx.project->settings.width);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Width")) ctx.project->settings.width = 1280;
    ImGui::DragInt("Window height", &ctx.project->settings.height);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Height")) ctx.project->settings.height = 1280;

    
    ImGui::DragFloat("LOD Bias", &ctx.renderer->lod_bias);
    ImGui::SameLine();
    if (ImGui::Button("Reset##LOD Bias")) ctx.renderer->lod_bias = 1280;
}

}