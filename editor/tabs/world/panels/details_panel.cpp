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
#include <editor/tabs/prefab/prefab_tab.h>
#include <core/world/prefab.h>
#include <cstring>
#include <core/rendering/world_gpu.h>
#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

void DetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.prefab_tab && ctx.world_tab && !ctx.world_tab->has_world()) {
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
    if (ctx.selection->player) {
        PlayerSettings& player = world.player;
        CameraSettings& cam = player.camera;
        ImGui::TextDisabled("Player");
        ImGui::Spacing();
        ui_field_push_style();
        if (ui_category("Control")) {
            const Entity current = world.find(player.controlled);
            const NameComponent* current_name = world.valid(current) ? world.try_get<NameComponent>(current) : nullptr;
            const char* preview = !player.controlled.valid() ? "Automatic" : (current_name && current_name->name[0] ? current_name->name : (world.valid(current) ? "Entity" : "Missing"));
            if (ImGui::BeginCombo("Character", preview)) {
                if (ImGui::Selectable("Automatic", !player.controlled.valid())) player.controlled = Guid{};
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Control the first entity with a Character component");
                world.view<CharacterComponent, EntityIdComponent>([&](Entity p_entity, const CharacterComponent&, const EntityIdComponent& p_id) {
                    const NameComponent* n = world.try_get<NameComponent>(p_entity);
                    ImGui::PushID((int)p_entity.index);
                    if (ImGui::Selectable(n && n->name[0] ? n->name : "Entity", player.controlled == p_id.guid)) player.controlled = p_id.guid;
                    ImGui::PopID();
                });
                ImGui::EndCombo();
            }
        }
        if (ui_category("Camera")) {
            ImGui::DragFloat("Pivot Height", &cam.pivot_height, 0.01f, 0.0f, 10.0f, "%.2f m");
            ImGui::DragFloat("Pivot Lag", &cam.pivot_lag, 0.005f, 0.0f, 1.0f, "%.3f s");
            ImGui::DragFloat("Vertical Lag", &cam.pivot_lag_vertical, 0.005f, 0.0f, 1.0f, "%.3f s");
            ImGui::DragFloat("Pitch Min", &cam.pitch_min, 0.5f, -89.0f, cam.pitch_max, "%.1f deg");
            ImGui::DragFloat("Pitch Max", &cam.pitch_max, 0.5f, cam.pitch_min, 89.0f, "%.1f deg");
            ImGui::DragFloat("Start Pitch", &cam.start_pitch, 0.5f, cam.pitch_min, cam.pitch_max, "%.1f deg");
            ImGui::DragFloat("Distance Scale", &cam.distance_scale, 0.01f, 0.25f, 4.0f, "%.2f");
        }
        if (ui_category("Look")) {
            ImGui::DragFloat("Mouse Sensitivity", &cam.mouse_sensitivity, 0.001f, 0.001f, 1.0f, "%.3f");
            ImGui::DragFloat("Stick Sensitivity", &cam.stick_sensitivity, 1.0f, 1.0f, 720.0f, "%.0f deg/s");
            ImGui::Checkbox("Invert Y", &cam.invert_y);
        }
        if (ui_category("Camera Collision")) {
            ImGui::DragFloat("Probe Radius", &cam.probe_radius, 0.005f, 0.01f, 2.0f, "%.2f m");
            ImGui::DragFloat("Return Time", &cam.return_time, 0.005f, 0.0f, 5.0f, "%.2f s");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How long the camera takes to ease back out after a wall");
        }
        if (ui_category("Camera States")) {
            static int edit_state = 0;
            static const char* STATE_NAMES[CAMERA_STATE_COUNT] = { "Explore", "Sprint", "Block", "Aim", "Lock-On", "Mounted" };
            ImGui::Combo("State", &edit_state, STATE_NAMES, (int)CAMERA_STATE_COUNT);
            CameraStateSettings& s = cam.states[std::clamp(edit_state, 0, (int)CAMERA_STATE_COUNT - 1)];
            ImGui::DragFloat("Distance", &s.distance, 0.01f, 0.0f, 50.0f, "%.2f m");
            ImGui::DragFloat2("Offset", &s.offset.x, 0.01f, -5.0f, 5.0f, "%.2f m");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Right and up from the pivot");
            ImGui::DragFloat("Field of View", &s.fov, 0.1f, 10.0f, 120.0f, "%.1f deg");
            ImGui::DragFloat("Enter Time", &s.enter_time, 0.005f, 0.0f, 5.0f, "%.2f s");
            ImGui::DragFloat("Exit Time", &s.exit_time, 0.005f, 0.0f, 5.0f, "%.2f s");
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

    const NameComponent* name = world.try_get<NameComponent>(e);
    ImGui::TextUnformatted(name && name->name[0] ? name->name : "Entity");
    Prefab* prefab = prefab_for(world, e);
    const uint64_t overrides = prefab ? prefab_overrides(world, e, *prefab) : 0;
    const auto component = [&](const char* p_key) {
        for (uint32_t i = 0; i < (uint32_t)world.components.size(); i++) {
            if (world.components[i].key && std::strcmp(world.components[i].key, p_key) == 0) return i;
        }
        return UINT32_MAX;
    };
    const auto overridden = [&](uint32_t p_index) { return p_index < 64 && (overrides >> p_index) & 1ull; };
    bool revert_transform = false;
    bool revert_model = false;
    bool revert_grid = false;
    const uint32_t transform_index = component("transform");
    const uint32_t model_index = component("model");
    const uint32_t grid_index = component("model_grid");
    const uint32_t animation_index = component("animation");
    const uint32_t collision_index = component("collision");
    const uint32_t character_index = component("character");
    const uint32_t static_index = component("static");

    bool revert_static = false;
    bool revert_entity = false;
    bool unpack = false;

    ui_field_push_style();
    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        if (ui_category("Transform", nullptr, overridden(transform_index) || overridden(static_index) ? &revert_transform : nullptr)) {
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
            bool is_static = world.has<StaticTag>(e);
            if (ImGui::Checkbox("Static", &is_static)) {
                if (is_static) world.deferred_add<StaticTag>(e);
                else world.deferred_remove<StaticTag>(e);
            }
            if (prefab && overridden(static_index)) {
                ImGui::SameLine();
                ui_push_buttons(UiButton::NEUTRAL);
                revert_static = ImGui::SmallButton(ICON_FA_ROTATE_LEFT "##revert_static");
                ui_pop_buttons();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to the prefab's value");
            }
        }
    }

    if (prefab && ui_category("Prefab")) {
        const UiStyle& ui = ui_style();
        const bool part = prefab_is_part(world, e);
        ImGui::TextColored(ui_vec4(ui.color.asset_prefab), ICON_FA_BOX "  %s", prefab->path.stem().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(part ? "part of an instance" : "instance");

        ui_push_buttons(UiButton::NEUTRAL);
        if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Open Prefab")) editor_open_prefab(ctx, prefab->guid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Edit and save the prefab in its own tab");
        if (part) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_POINTER "  Select Instance")) ctx.selection->select(prefab_root_of(world, e));
        }
        ImGui::BeginDisabled(overrides == 0 && part);
        if (ImGui::Button(part ? ICON_FA_ROTATE_LEFT "  Revert Entity" : ICON_FA_ROTATE_LEFT "  Revert Instance")) revert_entity = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(part ? "Reset this entity to the prefab" : "Reset the whole instance to the prefab");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_BOX_OPEN "  Unpack")) unpack = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn the instance into plain entities that no longer follow the prefab");
        ui_pop_buttons();

        ImGui::Spacing();
        if (overrides == 0) {
            ImGui::TextDisabled("No overrides on this entity");
        } else {
            ImGui::TextDisabled("Overrides");
            std::vector<std::string> fields;
            for (uint32_t i = 0; i < (uint32_t)world.components.size() && i < 64; i++) {
                if (!overridden(i)) continue;
                const ComponentInfo& info = world.components[i];
                prefab_overridden_fields(world, e, *prefab, i, fields);
                std::string text = info.name;
                if (!fields.empty()) {
                    text += ": ";
                    for (size_t f = 0; f < fields.size(); f++) text += (f ? ", " : "") + fields[f];
                } else if (std::strcmp(info.key, "parent") == 0) {
                    text += ": moved";
                } else {
                    text += info.has(world, e) ? ": added" : ": removed";
                }
                ImGui::TextColored(ui_vec4(ui.color.accent), "%s", text.c_str());
            }
        }
    }

    if (ModelComponent* model = world.try_get<ModelComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Model", &remove, overridden(model_index) ? &revert_model : nullptr);
        if (o && asset_field(ctx, "Model", "###model_field", model->model, AssetType::MODEL)) {
            world.touch(e);
        }
        if (remove) world.deferred_remove<ModelComponent>(e);
    }

    if (ModelGridComponent* grid = world.try_get<ModelGridComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Model Grid", &remove, overridden(grid_index) ? &revert_grid : nullptr);
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

    bool revert_animation = false;
    if (AnimationComponent* anim = world.try_get<AnimationComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Animation", &remove, overridden(animation_index) ? &revert_animation : nullptr);
        if (o && asset_field(ctx, "Blend Space", "###animation_blend_space", anim->blend_space, AssetType::BLEND_SPACE)) {
            anim->phase = 0.0f;
            world.touch(e);
        }
        if (remove) world.deferred_remove<AnimationComponent>(e);
    }

    bool revert_collision = false;
    if (CollisionComponent* col = world.try_get<CollisionComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Collision", &remove, overridden(collision_index) ? &revert_collision : nullptr);
        if (o) {
            bool changed = false;
            static const char* MOTIONS[] = { "Static", "Kinematic", "Dynamic" };
            int motion = (int)col->motion;
            if (ImGui::Combo("Motion", &motion, MOTIONS, 3)) {
                col->motion = (PhysicsMotion)motion;
                changed = true;
            }
            if (col->motion == PhysicsMotion::DYNAMIC) changed |= ImGui::DragFloat("Mass", &col->mass, 0.1f, 0.01f, 100000.0f, "%.2f kg");
            changed |= ImGui::SliderFloat("Friction", &col->friction, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Bounciness", &col->restitution, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::Checkbox("Trigger", &col->trigger);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Detects overlaps without blocking anything");

            static const char* TYPES[] = { "Box", "Sphere", "Capsule", "Model" };
            uint32_t remove_shape = UINT32_MAX;
            col->shape_count = std::min(col->shape_count, COLLISION_MAX_SHAPES);
            for (uint32_t i = 0; i < col->shape_count; i++) {
                CollisionShapeEntry& s = col->shapes[i];
                ImGui::PushID((int)i);
                ImGui::Spacing();
                ImGui::TextDisabled("Shape %u", i + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove_shape = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Remove this shape");
                int type = (int)s.type;
                if (ImGui::Combo("Type", &type, TYPES, (int)CollisionShapeType::COUNT)) {
                    s.type = (CollisionShapeType)type;
                    changed = true;
                }
                changed |= ImGui::DragFloat3("Position", &s.position.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
                changed |= ImGui::DragFloat3("Rotation", &s.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
                switch (s.type) {
                    case CollisionShapeType::BOX:
                        changed |= ImGui::DragFloat3("Half Extents", &s.half_extents.x, 0.01f, 0.001f, 1000.0f, "%.2f");
                        break;
                    case CollisionShapeType::SPHERE:
                        changed |= ImGui::DragFloat("Radius", &s.radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        break;
                    case CollisionShapeType::CAPSULE:
                        changed |= ImGui::DragFloat("Radius", &s.radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        changed |= ImGui::DragFloat("Half Height", &s.half_height, 0.01f, 0.001f, 1000.0f, "%.2f");
                        break;
                    case CollisionShapeType::MODEL:
                        if (!world.has<ModelComponent>(e)) ImGui::TextColored(ui_vec4(ui_style().color.danger), "Needs a Model component");
                        else ImGui::TextDisabled("Uses the model's imported collision");
                        break;
                    default:
                        break;
                }
                ImGui::PopID();
            }
            if (remove_shape != UINT32_MAX) {
                for (uint32_t i = remove_shape; i + 1 < col->shape_count; i++) col->shapes[i] = col->shapes[i + 1];
                col->shapes[col->shape_count - 1] = CollisionShapeEntry{};
                col->shape_count--;
                changed = true;
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(col->shape_count >= COLLISION_MAX_SHAPES);
            ui_push_buttons(UiButton::NEUTRAL);
            if (ImGui::Button(ICON_FA_PLUS "  Add Shape", ImVec2(-FLT_MIN, 0.0f))) {
                col->shapes[col->shape_count] = CollisionShapeEntry{};
                col->shape_count++;
                changed = true;
            }
            ui_pop_buttons();
            ImGui::EndDisabled();
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<CollisionComponent>(e);
    }

    bool revert_character = false;
    if (CharacterComponent* ch = world.try_get<CharacterComponent>(e)) {
        bool remove = false;
        const bool o = ui_category("Character", &remove, overridden(character_index) ? &revert_character : nullptr);
        if (o) {
            bool changed = false;
            ImGui::TextDisabled("Capsule");
            changed |= ImGui::DragFloat("Radius", &ch->radius, 0.005f, 0.05f, 5.0f, "%.2f m");
            changed |= ImGui::DragFloat("Height", &ch->height, 0.01f, ch->radius * 2.0f, 10.0f, "%.2f m");
            changed |= ImGui::DragFloat("Max Slope", &ch->max_slope, 0.5f, 0.0f, 89.0f, "%.0f deg");
            changed |= ImGui::DragFloat("Step Height", &ch->step_height, 0.01f, 0.0f, 2.0f, "%.2f m");
            ImGui::Spacing();
            ImGui::TextDisabled("Movement");
            changed |= ImGui::DragFloat("Walk Speed", &ch->walk_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
            changed |= ImGui::DragFloat("Jog Speed", &ch->jog_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
            changed |= ImGui::DragFloat("Sprint Speed", &ch->sprint_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
            changed |= ImGui::DragFloat("Acceleration", &ch->acceleration, 0.1f, 0.0f, 500.0f, "%.1f m/s2");
            changed |= ImGui::DragFloat("Deceleration", &ch->deceleration, 0.1f, 0.0f, 500.0f, "%.1f m/s2");
            changed |= ImGui::SliderFloat("Air Control", &ch->air_control, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::DragFloat("Turn Speed", &ch->turn_speed, 1.0f, 0.0f, 3600.0f, "%.0f deg/s");
            changed |= ImGui::DragFloat("Jump Speed", &ch->jump_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
            changed |= ImGui::DragFloat("Gravity Scale", &ch->gravity_scale, 0.01f, 0.0f, 10.0f, "%.2f");
            changed |= ImGui::DragFloat("Model Yaw Offset", &ch->yaw_offset, 1.0f, -180.0f, 180.0f, "%.0f deg");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn this if the model faces the wrong way while moving");
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<CharacterComponent>(e);
    }

    ui_category_flush();
    ui_field_pop_style();
    if (prefab && revert_transform) {
        prefab_revert_component(world, e, *prefab, transform_index);
        prefab_revert_component(world, e, *prefab, static_index);
    }
    if (prefab && revert_static) prefab_revert_component(world, e, *prefab, static_index);
    if (prefab && revert_model) prefab_revert_component(world, e, *prefab, model_index);
    if (prefab && revert_grid) prefab_revert_component(world, e, *prefab, grid_index);
    if (prefab && revert_animation) prefab_revert_component(world, e, *prefab, animation_index);
    if (prefab && revert_collision) prefab_revert_component(world, e, *prefab, collision_index);
    if (prefab && revert_character) prefab_revert_component(world, e, *prefab, character_index);
    if (revert_entity) editor_revert_prefab(ctx, e);
    if (unpack) editor_unpack_prefab(ctx, e);
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
