#include <editor/docking/panels/outliner_panel.h>
#include <editor/editor_context.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

void OutlinerPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.world || !ctx.selected) return;
    World& world = *ctx.world;
    Entity& selected = *ctx.selected;
    if (!world.valid(selected)) selected = ENTITY_NULL;

    const std::vector<Entity>* entities = world.entities_with<EntityIdComponent>();
    const int count = entities ? (int)entities->size() : 0;

    ImGui::TextDisabled("%d entities", count);
    ImGui::Separator();

    if (!ImGui::BeginChild("##outliner_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    ImGuiListClipper clipper;
    clipper.Begin(count);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const Entity e = (*entities)[i];
            const EntityIdComponent* id = world.try_get<EntityIdComponent>(e);
            char guid[Guid::BUFFER];
            id->guid.to_chars(guid);
            char label[64];
            snprintf(label, sizeof(label), "Entity %u  %s###%u", e.index, guid, e.index);
            if (ImGui::Selectable(label, e == selected)) selected = e;
        }
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) selected = ENTITY_NULL;
    ImGui::EndChild();
}

}