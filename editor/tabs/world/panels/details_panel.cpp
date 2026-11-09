#include <editor/tabs/world/panels/details_panel.h>
#include <editor/application/editor_context.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/asset_field.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/tabs/world/world_tab.h>
#include <core/rendering/world_gpu.h>
#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

void DetailsPanel::draw_contents(EditorContext& ctx)
{
    if (ctx.world_tab && !ctx.world_tab->has_world()) {
        ImGui::TextDisabled("No world open");
        return;
    }
    if (!ctx.world || !ctx.selection) return;
    World& world = *ctx.world;
    if (ctx.selection->environment) {
        EnvironmentSettings& env = world.environment;
        ImGui::TextDisabled("Environment");
        ImGui::Spacing();
        ui_field_push_style();
        if (ui_category("Exposure")) {
            ImGui::DragFloat("EV100", &env.manual_ev100, 0.05f, -6.0f, 18.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        ui_category_flush();
        ui_field_pop_style();
        return;
    }
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

    ui_field_push_style();
    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        if (ui_category("Transform")) {
            bool changed = false;
            changed |= ui_drag_float3_axes("Position", &xf->position.x, 0.1f);
            if (rotation_entity != e || rotation_seen != xf->rotation) {
                rotation_euler = degrees(eulerAngles(xf->rotation));
                rotation_entity = e;
                rotation_seen = xf->rotation;
            }
            if (ui_drag_float3_axes("Rotation", &rotation_euler.x, 0.5f)) {
                xf->rotation = quat(radians(rotation_euler));
                rotation_seen = xf->rotation;
                changed = true;
            }
            changed |= ui_drag_float3_axes("Scale", &xf->scale.x, 0.01f);
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

    if (ModelComponent* model = world.try_get<ModelComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Model", &remove);
        if (o && asset_field(ctx, "Model", "###model_field", model->model, AssetType::MODEL)) {
            world.touch(e);
        }
        if (remove) world.deferred_remove<ModelComponent>(e);
    }

    if (ModelGridComponent* grid = world.try_get<ModelGridComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Model Grid", &remove);
        if (o) {
            bool changed = asset_field(ctx, "Model", "###model_grid_field", grid->model, AssetType::MODEL);
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
        if (remove) world.deferred_remove<ModelGridComponent>(e);
    }

    ui_category_flush();
    ui_field_pop_style();
    ImGui::Dummy(ImVec2(0.0f, ui_style().panel_box.gap));

    ImGui::PushID("Add Component");
    ui_push_buttons(UiButton::NEUTRAL);
    const bool add_pressed = ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f));
    ui_pop_buttons();
    if (add_pressed) ImGui::OpenPopup("##add_component");
    ui_menu_push_style();
    if (ImGui::BeginPopup("##add_component")) {
        bool any = false;
        for (uint32_t i = 0; i < (uint32_t)world.components.size(); i++) {
            const ComponentInfo& info = world.components[i];
            if (!info.addable || info.has(world, e)) continue;
            any = true;
            if (ImGui::MenuItem(info.name)) world.deferred_add_component(e, i);
        }
        if (!any) ImGui::TextDisabled("Nothing to add");
        ImGui::EndPopup();
    }
    ui_menu_pop_style();
    ImGui::PopID();
}

}
