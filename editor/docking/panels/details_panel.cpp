#include <editor/docking/panels/details_panel.h>
#include <editor/editor_context.h>
#include <core/world/world.h>
#include <imgui.h>

namespace lumen {

bool DetailsPanel::_component_begin(const char* p_title)
{
    ImGui::PushID(p_title);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
    ImGui::BeginChild("##component", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    const bool op = ImGui::TreeNodeEx(p_title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding);
    if (op) ImGui::Indent(4.0f);
    return op;
}

bool DetailsPanel::_component_end(bool p_open, bool p_deletable)
{
    bool remove = false;
    if (p_open) {
        if (p_deletable) {
            const float size = ImGui::GetFrameHeight();
            ImGui::Spacing();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - size) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.15f, 0.15f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 0.80f));
            remove = ImGui::Button(ICON_FA_TRASH_CAN, ImVec2(size, size));
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        }
        ImGui::Unindent(4.0f);
        ImGui::TreePop();
        ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return remove;
}

void DetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.world || !ctx.selected) return;
    World& world = *ctx.world;
    const Entity e = *ctx.selected;
    if (!world.valid(e)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    if (const EntityIdComponent* id = world.try_get<EntityIdComponent>(e)) {
        char guid[Guid::BUFFER];
        id->guid.to_chars(guid);
        ImGui::TextDisabled("Entity %u  %s", e.index, guid);
    }
    ImGui::Spacing();

    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        const bool o = _component_begin("Transform");
        if (o) {
            ImGui::DragFloat3("##Position", &xf->position.x, 0.1f);
            if (rotation_entity != e || rotation_seen != xf->rotation) {
                rotation_euler = degrees(eulerAngles(xf->rotation));
                rotation_entity = e;
                rotation_seen = xf->rotation;
            }
            if (ImGui::DragFloat3("##Rotation", &rotation_euler.x, 0.5f)) {
                xf->rotation = quat(radians(rotation_euler));
                rotation_seen = xf->rotation;
            }
            ImGui::DragFloat3("Scale", &xf->scale.x, 0.01f);
            
        }
        _component_end(o, false);
    }

    if (const MeshComponent* mesh = world.try_get<MeshComponent>(e)) {
        const bool o = _component_begin("Mesh");
        if (o) {
            char guid[Guid::BUFFER];
            mesh->mesh.to_chars(guid);
            
            ImGui::TextUnformatted(guid);
            
        }
        if (_component_end(o)) world.deferred_remove<MeshComponent>(e);
    }
}

}