#include <editor/docking/center_view/profiler/profiler.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <imgui.h>
#include <implot.h>

namespace lumen {

void ProfilerDebugTab::draw(EditorContext& ctx)
{
    auto& profiler = ctx.renderer->graph.profiler;

    ImVec2 avail = ImGui::GetContentRegionAvail();

    const float right_width = avail.x * 0.2f;
    const float leftWidth = avail.x - right_width - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginGroup();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("TopLeft", ImVec2(leftWidth, avail.y), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    {
        timeline.draw(ctx);
    }
    ImGui::EndChild();

    // ImGui::BeginChild("BottomLeft", ImVec2(leftWidth, bottomHeight), ImGuiChildFlags_Borders);
    // {
    //     if (ImGui::BeginTabBar("Tabs")) {
    //         if (ImGui::BeginTabItem("Resources")) {
    //             resources.draw(ctx, timeline.selected_pass ? timeline.selected_pass->name : nullptr);
    //             ImGui::EndTabItem();
    //         }
    //         if (ImGui::BeginTabItem("Samplers")) {
    //             ImGui::EndTabItem();
    //         }
    //         if (ImGui::BeginTabItem("Blend State")) {
    //             ImGui::EndTabItem();
    //         }
    //         if (ImGui::BeginTabItem("Shader State")) {
    //             ImGui::EndTabItem();
    //         }
    //         ImGui::EndTabBar();
    //     }
    // }
    // ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::SameLine();
    
    ImGui::BeginChild("Right", ImVec2(right_width, avail.y), ImGuiChildFlags_Borders);
    {
        ImGui::BeginDisabled(!profiler.supported);
        ImGui::Checkbox("Enable Profiling", &profiler.enabled);
        ImGui::EndDisabled();

        imgui_title("Legend");
        imgui_property_row_value_aligned("Pan Area", "ALT + Mouse");
        imgui_property_row_value_aligned("Navigate", "Mouse Scroll");
        imgui_property_row_value_aligned("Zoom Area", "Mouse Drag");
        imgui_property_row_value_aligned("Zoom Out", "Double Click");
        imgui_property_row_value_aligned("Frame Pass", "F");
        imgui_property_row_value_aligned("Record", "Space");
        imgui_property_row_value_aligned("Resume", "ESC");
        imgui_spacing();

        distribution.draw(ctx, timeline.selected_draw, profiler.frozen);

        resources.draw(ctx, timeline.selected_pass ? timeline.selected_pass->name : nullptr);
    }
    ImGui::EndChild();
}

}