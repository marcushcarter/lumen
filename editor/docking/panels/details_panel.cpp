#include <editor/docking/panels/details_panel.h>
#include <editor/editor_context.h>
#include <editor/docking/tab_strip.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/world/editor_selection.h>
#include <core/rendering/world_gpu.h>
#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <imgui.h>

namespace lumen {

static ImVec4 _accent_tint(float p_t)
{
    const DockColors dc = DockColors::get();
    const ImVec4 card = ImGui::ColorConvertU32ToFloat4(dc.strip);
    const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(dc.accent);
    return ImVec4(card.x + (accent.x - card.x) * p_t, card.y + (accent.y - card.y) * p_t, card.z + (accent.z - card.z) * p_t, 1.0f);
}

bool DetailsPanel::_component_begin(const char* p_title)
{
    const DockColors dc = DockColors::get();
    const ImVec4 card = ImGui::ColorConvertU32ToFloat4(dc.strip);
    const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(dc.accent);

    ImGui::PushID(p_title);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, dc.strip);
    ImGui::BeginChild("##component", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    ImGui::PopStyleColor();
    dock_field_push_style();
    ImGui::PushStyleColor(ImGuiCol_Header, _accent_tint(0.18f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, _accent_tint(0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, _accent_tint(0.42f));
    const bool op = ImGui::TreeNodeEx(p_title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NoTreePushOnOpen);
    ImGui::PopStyleColor(3);

    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, mx.y - 2.0f), mx, dc.accent);
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
    dock_field_pop_style();
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

bool DetailsPanel::_ies_field(EditorContext& ctx, Guid& r_ies)
{
    bool changed = false;
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(r_ies) : nullptr;
    char label[160];
    if (asset) {
        snprintf(label, sizeof(label), "%s###ies_field", asset->name.c_str());
    } else {
        char guid[Guid::BUFFER];
        r_ies.to_chars(guid);
        snprintf(label, sizeof(label), "%s%s###ies_field", guid, r_ies == Guid{} ? "" : " (missing)");
    }
    ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
    // if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset ? asset->asset_path.c_str() : "Drop a ies asset here");
    // if (ImGui::BeginDragDropTarget()) {
    //     if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
    //         if (((const AssetDragPayload*)peek->Data)->type == AssetType::IES) {
    //             if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
    //                 r_ies = ((const AssetDragPayload*)payload->Data)->guid;
    //                 changed = true;
    //             }
    //         }
    //     }
    //     ImGui::EndDragDropTarget();
    // }
    return changed;
}

void DetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.world || !ctx.selection) return;
    World& world = *ctx.world;
    const Entity e = ctx.selection->primary();
    if (!world.valid(e)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }
    if (ctx.selection->entities.size() > 1) ImGui::TextDisabled("%u entities selected, editing the last one", (uint32_t)ctx.selection->entities.size());

    if (const EntityIdComponent* id = world.try_get<EntityIdComponent>(e)) {
        char guid[Guid::BUFFER];
        id->guid.to_chars(guid);
        ImGui::TextDisabled("Entity %u  %s", e.index, guid);
    }
    if (const NameComponent* name = world.try_get<NameComponent>(e)) {
        ImGui::TextDisabled("%s", name->name);
    }
    bool is_static = world.has<StaticTag>(e);
    if (ImGui::Checkbox("Static", &is_static)) {
        if (is_static) world.deferred_add<StaticTag>(e);
        else world.deferred_remove<StaticTag>(e);
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
        }
        _component_end(o, false);
    }

    if (MeshComponent* mesh = world.try_get<MeshComponent>(e)) {
        const bool o = _component_begin("Mesh");
        if (o && _mesh_field(ctx, mesh->mesh)) {
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

    if (LightComponent* light = world.try_get<LightComponent>(e)) {
        const bool o = _component_begin("Light");
        if (o) {
            bool changed = false;
            int type = (int)light->type;
            if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
                light->type = (LightType)type;
                changed = true;
            }
            changed |= ImGui::ColorEdit3("Color", &light->color.x);
            changed |= ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);

            switch (light->type) {
                case LightType::DIRECTIONAL:
                    changed |= ImGui::DragFloat("Angular Diameter", &light->angular_diameter, 0.01f, 0.0f, 10.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
                    break;
                case LightType::POINT:
                case LightType::SPOT:
                    changed |= ImGui::DragFloat("Range", &light->range, 0.1f, 0.01f, FLT_MAX, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Source Radius", &light->source_radius, 0.005f, 0.0f, light->range, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
                    if (light->type == LightType::SPOT) {
                        changed |= ImGui::DragFloat("Inner Angle", &light->inner_cone_angle, 0.1f, 0.0f, light->outer_cone_angle, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
                        changed |= ImGui::DragFloat("Outer Angle", &light->outer_cone_angle, 0.1f, 0.1f, 89.9f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
                    }
                    changed |= ImGui::DragFloat("Fade Start", &light->fade_start, 1.0f, 0.0f, FLT_MAX, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Fade End", &light->fade_end, 1.0f, 0.0f, FLT_MAX, light->fade_end > light->fade_start ? "%.0f m" : "off", ImGuiSliderFlags_AlwaysClamp);
                    break;
            }

            bool affects_gi = (light->flags & LightComponent::FLAG_AFFECTS_GI) != 0;
            if (ImGui::Checkbox("Affects GI", &affects_gi)) {
                light->flags = affects_gi ? (light->flags | LightComponent::FLAG_AFFECTS_GI) : (light->flags & ~LightComponent::FLAG_AFFECTS_GI);
                changed = true;
            }
            bool cast_shadows = (light->flags & LightComponent::FLAG_CAST_SHADOWS) != 0;
            if (ImGui::Checkbox("Cast Shadows", &cast_shadows)) {
                light->flags = cast_shadows ? (light->flags | LightComponent::FLAG_CAST_SHADOWS) : (light->flags & ~LightComponent::FLAG_CAST_SHADOWS);
                changed = true;
            }
            if (cast_shadows) {
                changed |= ImGui::DragFloat("Shadow Bias", &light->shadow_bias, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::DragFloat("Shadow Normal Bias", &light->shadow_normal_bias, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }
            if (changed) world.touch(e);
        }
        if (_component_end(o)) world.deferred_remove<LightComponent>(e);
    }

    if (LightProfileComponent* light_prof = world.try_get<LightProfileComponent>(e)) {
        const bool o = _component_begin("Light Profile");
        if (o) {
            bool changed = _ies_field(ctx, light_prof->ies);
            if (changed) world.touch(e);
        }
        if (_component_end(o)) world.deferred_remove<LightProfileComponent>(e);
    }

    ImGui::Spacing();

    ImGui::PushID("Add Component");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DockColors::get().strip);
    ImGui::BeginChild("##component", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Button, _accent_tint(0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, _accent_tint(0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, _accent_tint(0.42f));
    const bool add_pressed = ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f));
    ImGui::PopStyleColor(3);
    {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, mx.y - 2.0f), mx, DockColors::get().accent);
    }
    if (add_pressed) ImGui::OpenPopup("##add_component");
    dock_menu_push_style();
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
    dock_menu_pop_style();
    ImGui::EndChild();
    ImGui::PopID();
}

}
