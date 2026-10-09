#include <editor/docking/panels/details_panel.h>
#include <editor/editor_context.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <core/rendering/world_gpu.h>
#include <core/world/world.h>
#include <imgui.h>

namespace lumen {

bool DetailsPanel::_component_begin(const char* p_title)
{
    ImGui::PushID(p_title);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
    ImGui::BeginChild("##component", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    const bool op = ImGui::TreeNodeEx(p_title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NoTreePushOnOpen);
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
        ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return remove;
}

bool DetailsPanel::_mesh_field(EditorContext& ctx, Guid& r_mesh)
{
    bool changed = false;
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(r_mesh) : nullptr;
    char label[160];
    if (asset) {
        snprintf(label, sizeof(label), "%s###mesh_field", asset->name.c_str());
    } else {
        char guid[Guid::BUFFER];
        r_mesh.to_chars(guid);
        snprintf(label, sizeof(label), "%s%s###mesh_field", guid, r_mesh == Guid{} ? "" : " (missing)");
    }
    ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset ? asset->asset_path.c_str() : "Drop a mesh asset here");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
            if (((const AssetDragPayload*)peek->Data)->type == AssetType::MESH) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
                    r_mesh = ((const AssetDragPayload*)payload->Data)->guid;
                    changed = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
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
    if (const NameComponent* name = world.try_get<NameComponent>(e)) {
        ImGui::TextDisabled("%s", name->name);
    }
    ImGui::Spacing();

    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        const bool o = _component_begin("Transform");
        if (o) {
            bool changed = false;
            changed |= ImGui::DragFloat3("Position", &xf->position.x, 0.1f);
            if (rotation_entity != e || rotation_seen != xf->rotation) {
                rotation_euler = degrees(eulerAngles(xf->rotation));
                rotation_entity = e;
                rotation_seen = xf->rotation;
            }
            if (ImGui::DragFloat3("Rotation", &rotation_euler.x, 0.5f)) {
                xf->rotation = quat(radians(rotation_euler));
                rotation_seen = xf->rotation;
                changed = true;
            }
            changed |= ImGui::DragFloat3("Scale", &xf->scale.x, 0.01f);
            if (changed) world.touch(e);

            bool is_static = world.has<StaticTag>(e);
            if (ImGui::Checkbox("Static", &is_static)) {
                if (is_static) world.deferred_add<StaticTag>(e);
                else world.deferred_remove<StaticTag>(e);
            }
        }
        _component_end(o, false);
    }

    if (MeshComponent* mesh = world.try_get<MeshComponent>(e)) {
        const bool o = _component_begin("Mesh");
        if (o && _mesh_field(ctx, mesh->mesh)) {
            mesh->mesh_index = MeshComponent::INVALID_INDEX;
            world.touch(e);
        }
        if (_component_end(o)) world.deferred_remove<MeshComponent>(e);
    }

    if (MeshGridComponent* grid = world.try_get<MeshGridComponent>(e)) {
        const bool o = _component_begin("Mesh Grid");
        if (o) {
            bool changed = _mesh_field(ctx, grid->mesh);
            int count[3] = { (int)grid->count.x, (int)grid->count.y, (int)grid->count.z };
            if (ImGui::DragInt3("Count", count, 0.2f, 1, 1024, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                grid->count = uvec3((uint32_t)count[0], (uint32_t)count[1], (uint32_t)count[2]);
                changed = true;
            }
            changed |= ImGui::DragFloat("Spacing", &grid->spacing, 0.1f, 0.0f, FLT_MAX, grid->spacing > 0.0f ? "%.2f" : "auto");
            const uint64_t total = (uint64_t)grid->count.x * grid->count.y * grid->count.z;
            if (total > MAX_INSTANCES) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%llu instances (over %u, not drawn)", (unsigned long long)total, MAX_INSTANCES);
            else ImGui::TextDisabled("%llu instances", (unsigned long long)total);
            if (changed) world.touch(e);
        }
        if (_component_end(o)) world.deferred_remove<MeshGridComponent>(e);
    }

    ImGui::Spacing();

    ImGui::PushID("Add Component");
    ImGui::BeginChild("##component", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    if (ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f))) ImGui::OpenPopup("##add_component");
    if (ImGui::BeginPopup("##add_component")) {
        bool any = false;
        for (uint32_t id = 0; id < (uint32_t)world.pools.size(); id++) {
            const PoolSlot& slot = world.pools[id];
            if (!slot.set || !slot.addable || world.masks[e.index].test(id)) continue;
            any = true;
            if (ImGui::MenuItem(slot.name)) world.deferred_add_id(e, id);
        }
        if (!any) ImGui::TextDisabled("Nothing to add");
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

}