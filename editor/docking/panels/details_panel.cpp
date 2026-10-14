#include <editor/docking/panels/details_panel.h>
#include <editor/editor_context.h>
#include <editor/docking/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/imgui/imgui_helpers.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/world/editor_selection.h>
#include <core/rendering/world_gpu.h>
#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

static void _push_accent_soft(ImGuiCol p_idle, ImGuiCol p_hovered, ImGuiCol p_active)
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(p_idle, ui.color.accent_soft);
    ImGui::PushStyleColor(p_hovered, ui.color.accent_soft_hover);
    ImGui::PushStyleColor(p_active, ui.color.accent_soft_active);
}

static void _accent_underline()
{
    const UiStyle& ui = ui_style();
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, mx.y - ui.details.line * 2.0f), mx, ui.color.accent);
}

bool DetailsPanel::_component_begin(const char* p_title, bool* r_remove)
{
    if (r_remove) *r_remove = false;
    return imgui_category(p_title, r_remove);
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset ? asset->asset_path.c_str() : "Drop a ies asset here");
    if (ImGui::BeginDragDropTarget()) {
    //     if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
    //         if (((const AssetDragPayload*)peek->Data)->type == AssetType::IES) {
    //             if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
    //                 r_ies = ((const AssetDragPayload*)payload->Data)->guid;
    //                 changed = true;
    //             }
    //         }
    //     }
        ImGui::EndDragDropTarget();
    }
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

    dock_field_push_style();
    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        if (_component_begin("Transform")) {
            bool changed = false;
            changed |= imgui_drag_float3_axes("Position", &xf->position.x, 0.1f);
            if (rotation_entity != e || rotation_seen != xf->rotation) {
                rotation_euler = degrees(eulerAngles(xf->rotation));
                rotation_entity = e;
                rotation_seen = xf->rotation;
            }
            if (imgui_drag_float3_axes("Rotation", &rotation_euler.x, 0.5f)) {
                xf->rotation = quat(radians(rotation_euler));
                rotation_seen = xf->rotation;
                changed = true;
            }
            changed |= imgui_drag_float3_axes("Scale", &xf->scale.x, 0.01f);
            if (changed) {
                for (int i = 0; i < 3; i++) {
                    if (!std::isfinite(xf->position[i]) || std::abs(xf->position[i]) > 1e6f) xf->position[i] = 0.0f;
                    if (!std::isfinite(xf->scale[i])) xf->scale[i] = 1.0f;
                }
                if (!std::isfinite(xf->rotation.x) || !std::isfinite(xf->rotation.y) || !std::isfinite(xf->rotation.z) || !std::isfinite(xf->rotation.w)) xf->rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
                world.touch(e);
            }
        }
    }

    if (MeshComponent* mesh = world.try_get<MeshComponent>(e)) {
        bool remove = false;
        const bool o = _component_begin("Mesh", &remove);
        if (o && _mesh_field(ctx, mesh->mesh)) {
            world.touch(e);
        }
        if (remove) world.deferred_remove<MeshComponent>(e);
    }

    if (MeshGridComponent* grid = world.try_get<MeshGridComponent>(e)) {
        bool remove = false;
        const bool o = _component_begin("Mesh Grid", &remove);
        if (o) {
            bool changed = _mesh_field(ctx, grid->mesh);
            int count[3] = { (int)grid->count.x, (int)grid->count.y, (int)grid->count.z };
            if (ImGui::DragInt3("Count", count, 0.2f, 1, 1024, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                grid->count = uvec3((uint32_t)count[0], (uint32_t)count[1], (uint32_t)count[2]);
                changed = true;
            }
            changed |= ImGui::DragFloat("Spacing", &grid->spacing, 0.1f, 0.0f, FLT_MAX, grid->spacing > 0.0f ? "%.2f" : "auto");
            const uint64_t total = (uint64_t)grid->count.x * grid->count.y * grid->count.z;
            if (total > MAX_INSTANCES) ImGui::TextColored(ui_vec4(ui_style().color.danger), "%llu instances (over %u, not drawn)", (unsigned long long)total, MAX_INSTANCES);
            else ImGui::TextDisabled("%llu instances", (unsigned long long)total);
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<MeshGridComponent>(e);
    }

    if (LightComponent* light = world.try_get<LightComponent>(e)) {
        bool remove = false;
        const bool o = _component_begin("Light", &remove);
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
        if (remove) world.deferred_remove<LightComponent>(e);
    }

    if (LightProfileComponent* light_prof = world.try_get<LightProfileComponent>(e)) {
        bool remove = false;
        const bool o = _component_begin("Light Profile", &remove);
        if (o) {
            bool changed = _ies_field(ctx, light_prof->ies);
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<LightProfileComponent>(e);
    }

    dock_field_pop_style();
    ImGui::Spacing();

    ImGui::PushID("Add Component");
    _push_accent_soft(ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive);
    const bool add_pressed = ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f));
    ImGui::PopStyleColor(3);
    _accent_underline();
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
    ImGui::PopID();
}

}
