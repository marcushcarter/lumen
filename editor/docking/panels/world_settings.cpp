#include <editor/docking/panels/world_settings.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <imgui.h>

namespace lumen {
    
void WorldSettingsPanel::draw_contents(EditorContext& ctx)
{
    ImGui::Text("World Settings");
    
    ImGui::BeginDisabled(true);
    ImGui::DragInt("Window width", &ctx.project->settings.width);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Width")) ctx.project->settings.width = 1280;
    ImGui::DragInt("Window height", &ctx.project->settings.height);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Height")) ctx.project->settings.height = 720;
    ImGui::EndDisabled();

    ImGui::DragFloat("LOD Bias", &ctx.renderer->lod_bias, 0.01f, 0.01f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset##LOD Bias")) ctx.renderer->lod_bias = 0.6f;
}

}