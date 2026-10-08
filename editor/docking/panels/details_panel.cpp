#include <editor/docking/panels/details_panel.h>
#include <editor/editor_context.h>
#include <imgui.h>

namespace lumen {

void DetailsPanel::draw_contents(EditorContext& )
{
    ImGui::PushID("component");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 3));
    ImGui::BeginChild("TransformChildWindow", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);

    bool treeOpen = ImGui::TreeNodeEx(
        "component",
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_Framed |
        ImGuiTreeNodeFlags_FramePadding
    );

    if (treeOpen) {
        ImGui::BeginChild(
            "TransformFields",
            ImVec2(ImGui::GetContentRegionAvail().x - 5.0f, 0),
            ImGuiChildFlags_AutoResizeY
        );

        // if (deletable) {
        // 	if (ImGui::Button("X")) entity.remove<T>();
        // }

        // if (contentFunc) contentFunc();

        ImGui::Text("Content");
        ImGui::Text("Content");
        ImGui::Text("Content");
        ImGui::Text("Content");
        ImGui::Text("Content");
        ImGui::Text("Content");

        ImGui::EndChild();
        ImGui::TreePop();
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

}