#include <editor/documents/scene/properties_panel.h>
#include <editor/application/editor_context.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/asset_field.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/documents/scene/scene_document.h>
#include <editor/documents/scene/template_document.h>
#include <editor/play/editor_play.h>
#include <editor/assets/import/collision_cooker.h>
#include <editor/viewport/shape_gizmo.h>
#include <editor/assets/import/mesh_cooker.h>
#include <core/rendering/renderer.h>
#include <core/scene/template.h>
#include <core/scene/scene.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

static bool g_section_open = false;

static void _section_end()
{
    if (!g_section_open) return;
    ui_property_grid_end();
    ui_list_body_end();
    g_section_open = false;
}

static bool _section(const char* p_label, bool* r_remove = nullptr, bool* r_revert = nullptr)
{
    _section_end();
    bool* open = ImGui::GetStateStorage()->GetBoolRef(ImGui::GetID(p_label), std::strcmp(p_label, "Entity Info") == 0);
    if (ui_list_item(p_label, nullptr, p_label, nullptr, *open, r_remove, r_revert)) *open = !*open;
    if (!*open) return false;
    ui_list_body_begin();
    if (!ui_property_grid_begin("##grid", 3)) {
        ui_list_body_end();
        return false;
    }
    g_section_open = true;
    return true;
}

static void _name(const char* p_name)
{
    ui_property(p_name);
}

static bool _check(const char* p_id, bool* r_value)
{
    return ImGui::Checkbox(p_id, r_value);
}

static void _live(_Printf_format_string_ const char* p_fmt, ...)
{
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextV(p_fmt, args);
    va_end(args);
}

static void _live_vec3(bool p_has, const vec3& p_v)
{
    if (p_has) _live("%.3f, %.3f, %.3f", p_v.x, p_v.y, p_v.z);
    else _live("-");
}

static void _live_float(bool p_has, float p_v, const char* p_unit = "")
{
    if (p_has) _live("%.3f%s", p_v, p_unit);
    else _live("-");
}

static constexpr double MESH_STATUS_POLL_S = 1.0;

static const char* _mesh_collision_status(EditorContext& ctx, const Scene& p_world, Entity p_entity)
{
    const ModelComponent* model = p_world.try_get<ModelComponent>(p_entity);
    if (!model || !ctx.renderer || !ctx.assets) return nullptr;
    const uint32_t mesh_index = ctx.renderer->models.mesh_index(model->model);
    if (!ctx.renderer->geometry.get(mesh_index)) return nullptr;
    const AssetRegistry::Entry* entry = ctx.assets->find(ctx.renderer->geometry.mesh_guids[mesh_index]);
    if (!entry) return nullptr;
    struct Cached { double time = -1.0e9; const char* status = nullptr; };
    static std::unordered_map<std::string, Cached> cache;
    Cached& c = cache[entry->path.string()];
    const double now = ImGui::GetTime();
    if (now - c.time < MESH_STATUS_POLL_S) return c.status;
    c.time = now;
    MeshCooker::CookSettings settings;
    std::filesystem::path source;
    MeshCooker::load_settings(entry->path, settings, source);
    c.status = CollisionCooker::has_collision(entry->path) ? collision_type_label(settings.collision.type) : nullptr;
    return c.status;
}

void PropertiesPanel::draw_contents(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    if (!ctx.template_doc && ctx.scene_document && !ctx.scene_document->has_world()) return;
    if (!ctx.scene || !ctx.selection) return;
    Scene& world = *ctx.scene;

    const bool live_on = ctx.play && ctx.play->active() && !ctx.template_doc;
    const Scene* live = live_on ? &ctx.play->runtime : nullptr;

    const Entity selected = ctx.selection->primary();
    const bool has_target = ctx.selection->scene || world.valid(selected);
    const float inset = ui_category_inset();
    {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float row = ui.row;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + w, o.y + row), ui.color.header);
        const float grid_x = o.x + inset + ui.space.xl;
        const float col_w = ImMax(1.0f, w - inset * 2.0f - ui.space.xl) / 3.0f;
        const float ty = ImFloor(o.y + (row - ImGui::GetFontSize()) * 0.5f);
        static constexpr const char* HEADERS[3] = { "Name", "Value", "Live Value" };
        for (int i = 0; i < 3; i++) dl->AddText(ImVec2(ImFloor(grid_x + col_w * (float)i + ImGui::GetStyle().CellPadding.x), ty), ui.color.text, HEADERS[i]);
        const ImVec2 cog(o.x + w - row, o.y);
        ImGui::SetCursorScreenPos(cog);
        if (ImGui::InvisibleButton("##properties_settings", ImVec2(row, row))) ImGui::OpenPopup("##properties_settings_menu");
        if (ImGui::IsItemHovered()) dl->AddRectFilled(cog, ImVec2(cog.x + row, cog.y + row), ui.color.tool_hover, ui.radius.sm);
        ui_text_centered(dl, cog, ImVec2(cog.x + row, cog.y + row), ui.color.text, ICON_FA_GEAR);
        ui_menu_push_style();
        if (ImGui::BeginPopup("##properties_settings_menu")) ImGui::EndPopup();
        ui_menu_pop_style();
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + row));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    if (!has_target) return;

    ui_field_push_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, ui.panel_box.gap));
    const bool shown = ImGui::BeginChild("##properties_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    g_section_open = false;
    if (!shown) {
        ImGui::EndChild();
        ui_field_pop_style();
        return;
    }

    if (ctx.selection->scene) {
        if (_section("Scene Info")) {
            _name("Name");
            ImGui::TextDisabled("%s", ctx.scene_document ? ctx.scene_document->document_name().c_str() : "-");
            _live("-");
            _name("Exposure");
            ImGui::TextDisabled("%.2f (fixed)", EnvironmentSettings::EXPOSURE);
            _live("-");
        }
        if (_section("Shadows")) {
            _name("Player Shadow");
            ImGui::Checkbox("##player_shadow", &world.shadows.player_shadow);
            _live("%s", live ? (live->shadows.player_shadow ? "On" : "Off") : "-");
            _name("Cascade 0 (8m)");
            ImGui::Checkbox("##cascade0_shadow", &world.shadows.cascade0_shadow);
            _live("%s", live ? (live->shadows.cascade0_shadow ? "On" : "Off") : "-");
            _name("Cascade 1 (80m)");
            ImGui::Checkbox("##cascade1_shadow", &world.shadows.cascade1_shadow);
            _live("%s", live ? (live->shadows.cascade1_shadow ? "On" : "Off") : "-");
            _name("Heightfield (1.5km)");
            ImGui::Checkbox("##heightfield_shadow", &world.shadows.heightfield_shadow);
            _live("%s", live ? (live->shadows.heightfield_shadow ? "On" : "Off") : "-");
        }
        if (_section("Physics")) {
            _name("Gravity");
            ImGui::DragFloat("##gravity", &world.physics.gravity, 0.05f, 0.0f, 100.0f, "%.2f m/s2");
            _live("%.2f m/s2", live ? live->physics.gravity : 0.0f);
        }
        if (_section("Terrain")) {
            _name("Terrain");
            asset_field(ctx, "", "###scene_terrain", world.terrain.terrain, AssetType::TERRAIN);
            const AssetRegistry::Entry* entry = live && ctx.assets ? ctx.assets->find(live->terrain.terrain) : nullptr;
            if (entry) _live("%s", entry->name.c_str());
            else _live("-");
        }
        WaterSettings& water = world.water;
        const WaterWorld* ww = live ? &ctx.play->physics_system.water : nullptr;
        const WaterSettings* lw = live ? &live->water : nullptr;
        const auto on_off = [](bool p_on) { return p_on ? "On" : "Off"; };
        if (_section("Water")) {
            _name("Simulation");
            _check("##water_simulate", &water.simulate);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Off freezes the water where it is and turns off buoyancy.");
            _live("%s", lw ? on_off(lw->simulate) : "-");
            _name("Rendering");
            _check("##water_render", &water.render);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Off hides the water. The simulation keeps running.");
            _live("%s", lw ? on_off(lw->render) : "-");
            _name("Water Asset");
            asset_field(ctx, "", "###water_bake", water.bake, AssetType::WATER);
            const AssetRegistry::Entry* bake_entry = lw && ctx.assets ? ctx.assets->find(lw->bake) : nullptr;
            if (bake_entry) _live("%s", bake_entry->name.c_str());
            else _live("-");
            _name("Bake Time");
            ImGui::DragFloat("##water_bake_time", &water.bake_time, 1.0f, 5.0f, 3600.0f, "%.0f s", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Simulated seconds the bake runs so rivers and lakes can settle.");
            _live_float(lw, lw ? lw->bake_time : 0.0f, " s");
            _name("");
            const bool baking = ww && ww->bake.active();
            ImGui::BeginDisabled(baking || !ctx.water_bake);
            if (ImGui::Button("Bake Water", ImVec2(-FLT_MIN, 0.0f)) && ctx.water_bake) ctx.water_bake();
            ImGui::EndDisabled();
            if (baking) _live("%s %.0f%%", ww->bake.collecting() ? "Saving" : "Baking", ww->bake.progress() * 100.0f);
            else if (ww && !ww->bake.message.empty()) _live("%s", ww->bake.message.c_str());
            else _live("-");
            _name("Caustics");
            _check("##water_caustics", &water.caustics);
            _live("%s", lw ? on_off(lw->caustics) : "-");
            _name("Cell Size");
            ImGui::DragFloat("##water_cell_size", &water.cell_size, 0.01f, 0.1f, 2.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Changing this restarts the simulation.");
            _live_float(lw, lw ? lw->cell_size : 0.0f, " m");
            _name("Objects Push Water");
            _check("##water_push", &water.push_water);
            _live("%s", lw ? on_off(lw->push_water) : "-");
            _name("Substeps");
            int substeps = (int)water.substeps;
            if (ImGui::DragInt("##water_substeps", &substeps, 0.05f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp)) water.substeps = (uint32_t)substeps;
            if (lw) _live("%u", lw->substeps);
            else _live("-");
            _name("Sim Radius");
            ImGui::DragFloat("##water_sim_radius", &water.sim_radius, 0.5f, 8.0f, 512.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("The simulation runs this far from the player. Water further out is frozen.");
            _live_float(lw, lw ? lw->sim_radius : 0.0f, " m");
            _name("Vorticity");
            ImGui::DragFloat("##water_vorticity", &water.vorticity, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lw, lw ? lw->vorticity : 0.0f);
            _name("Bed Probe Height");
            ImGui::DragFloat("##water_bed_probe", &water.bed_probe, 0.1f, 0.5f, 200.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far above the terrain the riverbed rays start. Must be above the tallest object standing in water.");
            _live_float(lw, lw ? lw->bed_probe : 0.0f, " m");
            _name("CFL Limit");
            ImGui::DragFloat("##water_cfl", &water.cfl_limit, 0.005f, 0.01f, 0.9f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lw, lw ? lw->cfl_limit : 0.0f);
            _name("Flux Depth Cap");
            ImGui::DragFloat("##water_flux_cap", &water.flux_depth_cap, 0.01f, 0.05f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lw, lw ? lw->flux_depth_cap : 0.0f);
            _name("Foam Decay");
            ImGui::DragFloat("##water_foam_decay", &water.foam_decay, 0.01f, 0.0f, 10.0f, "%.2f /s", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lw, lw ? lw->foam_decay : 0.0f, " /s");
            _name("Foam Gain");
            ImGui::DragFloat("##water_foam_gain", &water.foam_gain, 0.01f, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lw, lw ? lw->foam_gain : 0.0f);
        }
        _section_end();
        ImGui::EndChild();
        ui_field_pop_style();
        return;
    }

    const Entity e = selected;
    const Entity le = live ? ctx.play->to_runtime(e) : ENTITY_NULL;
    const bool has_live = live && live->valid(le);

    Template* tmpl = template_for(world, e);
    const uint64_t overrides = tmpl ? template_overrides(world, e, *tmpl) : 0;
    const auto component = [&](const char* p_key) {
        for (uint32_t i = 0; i < (uint32_t)world.components.size(); i++) {
            if (world.components[i].key && std::strcmp(world.components[i].key, p_key) == 0) return i;
        }
        return UINT32_MAX;
    };
    const auto overridden = [&](uint32_t p_index) { return p_index < 64 && (overrides >> p_index) & 1ull; };
    const uint32_t transform_index = component("transform");
    const uint32_t model_index = component("model");
    const uint32_t collision_index = component("collision");
    const uint32_t character_index = component("character");
    const uint32_t static_index = component("static");
    const uint32_t disabled_index = component("disabled");

    bool revert_transform = false;
    bool revert_model = false;
    bool revert_static = false;
    bool revert_visible = false;
    bool revert_entity = false;
    bool revert_character = false;
    bool revert_collision = false;
    bool unpack = false;

    if (_section("Entity Info")) {
        NameComponent* name = world.try_get<NameComponent>(e);
        _name("Name");
        if (name) {
            if (ImGui::InputText("##name", name->name, NameComponent::MAX_LENGTH)) world.touch(e);
        } else {
            ImGui::TextUnformatted("Entity");
        }
        const NameComponent* live_name = has_live ? live->try_get<NameComponent>(le) : nullptr;
        if (live_name) _live("%s", live_name->name);
        else _live("-");
        const EntityIdComponent* id = world.try_get<EntityIdComponent>(e);
        _name("Id");
        ImGui::TextDisabled("%s", id ? id->guid.to_string().c_str() : "-");
        _live("-");
    }

    const bool general_overridden = overridden(transform_index) || overridden(static_index) || overridden(disabled_index);
    if (_section("General", nullptr, general_overridden ? &revert_transform : nullptr)) {
        bool is_static = world.has<StaticTag>(e);
        _name("Static");
        if (_check("##static", &is_static)) {
            if (is_static) world.deferred_add<StaticTag>(e);
            else world.deferred_remove<StaticTag>(e);
        }
        if (tmpl && overridden(static_index)) {
            ImGui::SameLine();
            revert_static = ImGui::SmallButton(ICON_FA_ROTATE_LEFT "##revert_static");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to the template's value");
        }
        if (has_live) _live("%s", live->has<StaticTag>(le) ? "True" : "False");
        else _live("-");

        if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
            const TransformComponent* lxf = has_live ? live->try_get<TransformComponent>(le) : nullptr;
            bool changed = false;
            _name("Position");
            changed |= ImGui::DragFloat3("##position", &xf->position.x, 0.1f, 0.0f, 0.0f, "%.3f");
            _live_vec3(lxf != nullptr, lxf ? lxf->position : vec3(0.0f));

            if (rotation_entity != e || rotation_seen != xf->rotation) {
                rotation_euler = degrees(eulerAngles(xf->rotation));
                rotation_entity = e;
                rotation_seen = xf->rotation;
            }
            _name("Rotation");
            if (ImGui::DragFloat3("##rotation", &rotation_euler.x, 0.5f, 0.0f, 0.0f, "%.2f")) {
                xf->rotation = quat(radians(rotation_euler));
                rotation_seen = xf->rotation;
                changed = true;
            }
            _live_vec3(lxf != nullptr, lxf ? degrees(eulerAngles(lxf->rotation)) : vec3(0.0f));

            _name("Scale");
            changed |= ImGui::DragFloat3("##scale", &xf->scale.x, 0.01f, 0.0f, 0.0f, "%.3f");
            _live_vec3(lxf != nullptr, lxf ? lxf->scale : vec3(0.0f));

            if (changed) {
                for (int i = 0; i < 3; i++) {
                    if (!std::isfinite(xf->position[i]) || std::abs(xf->position[i]) > 1e6f) xf->position[i] = 0.0f;
                    if (!std::isfinite(xf->scale[i])) xf->scale[i] = 1.0f;
                }
                if (!std::isfinite(xf->rotation.x) || !std::isfinite(xf->rotation.y) || !std::isfinite(xf->rotation.z) || !std::isfinite(xf->rotation.w)) xf->rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
                world.touch(e);
            }
        }

        bool visible = !world.has<DisabledTag>(e);
        _name("Visible");
        if (_check("##visible", &visible)) {
            std::vector<Entity> subtree;
            world.subtree(e, subtree);
            for (const Entity c : subtree) {
                if (world.has<DisabledTag>(c) == !visible) continue;
                if (visible) world.deferred_remove<DisabledTag>(c);
                else world.deferred_add<DisabledTag>(c);
            }
        }
        if (tmpl && overridden(disabled_index)) {
            ImGui::SameLine();
            revert_visible = ImGui::SmallButton(ICON_FA_ROTATE_LEFT "##revert_visible");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to the template's value");
        }
        if (has_live) _live("%s", live->has<DisabledTag>(le) ? "False" : "True");
        else _live("-");
    }

    if (tmpl && _section("Template")) {
        const bool part = template_is_part(world, e);
        _name("Template");
        ImGui::TextColored(ui_vec4(ui.color.asset_template), "%s", tmpl->path.filename().string().c_str());
        _live("-");
        _name("Kind");
        ImGui::TextUnformatted(part ? "Part of an instance" : "Instance");
        _live("-");
        _name("Actions");
        if (ImGui::SmallButton("Open")) editor_open_template(ctx, tmpl->guid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Open the template as the document");
        if (part) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select Instance")) ctx.selection->select(template_root_of(world, e));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(overrides == 0 && part);
        if (ImGui::SmallButton("Revert")) revert_entity = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(part ? "Reset this entity to the template" : "Reset the whole instance to the template");
        ImGui::SameLine();
        if (ImGui::SmallButton("Unpack")) unpack = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn the instance into plain entities that no longer follow the template");
        _live("-");

        std::vector<std::string> fields;
        bool any = false;
        for (uint32_t i = 0; i < (uint32_t)world.components.size() && i < 64; i++) {
            if (!overridden(i)) continue;
            any = true;
            const ComponentInfo& info = world.components[i];
            template_overridden_fields(world, e, *tmpl, i, fields);
            std::string text;
            if (!fields.empty()) {
                for (size_t f = 0; f < fields.size(); f++) text += (f ? ", " : "") + fields[f];
            } else if (std::strcmp(info.key, "parent") == 0) {
                text = "moved";
            } else {
                text = info.has(world, e) ? "added" : "removed";
            }
            _name(info.name);
            ImGui::TextColored(ui_vec4(ui.color.accent), "%s", text.c_str());
            _live("-");
        }
        if (!any) {
            _name("Overrides");
            ImGui::TextDisabled("None");
            _live("-");
        }
    }

    if (ModelComponent* model = world.try_get<ModelComponent>(e)) {
        bool remove = false;
        if (_section("Model", &remove, overridden(model_index) ? &revert_model : nullptr)) {
            _name("Model");
            if (asset_field(ctx, "", "###model_field", model->model, AssetType::MODEL)) world.touch(e);
            const ModelComponent* lm = has_live ? live->try_get<ModelComponent>(le) : nullptr;
            const AssetRegistry::Entry* entry = lm && ctx.assets ? ctx.assets->find(lm->model) : nullptr;
            if (entry) _live("%s", entry->name.c_str());
            else _live("-");
            _name("Tint");
            if (ImGui::ColorEdit4("##model_tint", &model->tint.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) world.touch(e);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Multiplies the material colour. White leaves it unchanged.");
            if (lm) _live("%.2f, %.2f, %.2f", lm->tint.x, lm->tint.y, lm->tint.z);
            else _live("-");
        }
        if (remove) world.deferred_remove<ModelComponent>(e);
    }

    if (CharacterComponent* loco = world.try_get<CharacterComponent>(e)) {
        bool remove = false;
        const CharacterComponent* ll = has_live ? live->try_get<CharacterComponent>(le) : nullptr;
        if (_section("Character", &remove, overridden(character_index) ? &revert_character : nullptr)) {
            bool changed = false;
            _name("Controller");
            static constexpr const char* CONTROLLERS[] = { "None", "Player", "AI" };
            int controller = (int)loco->controller;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##controller", &controller, CONTROLLERS, IM_ARRAYSIZE(CONTROLLERS))) {
                loco->controller = (CharacterController)controller;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Player gets the input and the camera in play.");
            _live("%s", ll ? CONTROLLERS[(int)ll->controller] : "-");
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<CharacterComponent>(e);
    }

    const uint32_t orbit_index = component("orbit_camera");
    bool revert_orbit = false;
    if (OrbitCameraComponent* orbit = world.try_get<OrbitCameraComponent>(e)) {
        bool remove = false;
        const OrbitCameraComponent* lo = has_live ? live->try_get<OrbitCameraComponent>(le) : nullptr;
        if (_section("Orbit Camera", &remove, overridden(orbit_index) ? &revert_orbit : nullptr)) {
            bool changed = false;
            _name("Distance");
            changed |= ImGui::DragFloat("##orbit_distance", &orbit->distance, 0.01f, 0.1f, 100.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lo != nullptr, lo ? lo->distance : 0.0f, " m");
            _name("Shoulder Offset");
            changed |= ImGui::DragFloat("##orbit_shoulder", &orbit->shoulder, 0.01f, -5.0f, 5.0f, "%.2f m");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Sideways offset of the camera. Negative puts it over the left shoulder.");
            _live_float(lo != nullptr, lo ? lo->shoulder : 0.0f, " m");
            _name("Pivot Height");
            changed |= ImGui::DragFloat("##orbit_pivot", &orbit->pivot_height, 0.01f, -10.0f, 10.0f, "%.2f");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Where the camera looks, in metres above the entity.");
            _live_float(lo != nullptr, lo ? lo->pivot_height : 0.0f);
            _name("Collision Radius");
            changed |= ImGui::DragFloat("##orbit_radius", &orbit->radius, 0.005f, 0.0f, 2.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Size of the sphere that pulls the camera in front of walls");
            _live_float(lo != nullptr, lo ? lo->radius : 0.0f, " m");
            _name("Pitch Min");
            changed |= ImGui::DragFloat("##orbit_pitch_min", &orbit->pitch_min, 0.5f, -89.0f, orbit->pitch_max, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lo != nullptr, lo ? lo->pitch_min : 0.0f, " deg");
            _name("Pitch Max");
            changed |= ImGui::DragFloat("##orbit_pitch_max", &orbit->pitch_max, 0.5f, orbit->pitch_min, 89.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            _live_float(lo != nullptr, lo ? lo->pitch_max : 0.0f, " deg");
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<OrbitCameraComponent>(e);
    }

    const uint32_t water_index = component("water_source");
    bool revert_water = false;
    if (WaterSourceComponent* water = world.try_get<WaterSourceComponent>(e)) {
        bool remove = false;
        const WaterSourceComponent* lw = has_live ? live->try_get<WaterSourceComponent>(le) : nullptr;
        if (_section("Water Source", &remove, overridden(water_index) ? &revert_water : nullptr)) {
            bool changed = false;
            static constexpr const char* TYPES[] = { "Spring", "Level" };
            static_assert(std::size(TYPES) == (size_t)WaterSourceType::COUNT);
            int type = (int)water->type;
            _name("Type");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##water_type", &type, TYPES, IM_ARRAYSIZE(TYPES))) {
                water->type = (WaterSourceType)type;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Spring: adds water inside the box. Level: keeps the box filled to its top, for lakes and the sea.");
            _live("%s", lw ? TYPES[ImClamp((int)lw->type, 0, (int)WaterSourceType::COUNT - 1)] : "-");
            _name("Half Extents");
            if (ImGui::DragFloat3("##water_half_extents", &water->half_extents.x, 0.05f, 0.01f, 100000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) changed = true;
            _live_vec3(lw != nullptr, lw ? lw->half_extents : vec3(0.0f));
            if (water->type == WaterSourceType::SPRING) {
                _name("Flow");
                if (ImGui::DragFloat("##water_flow", &water->flow, 0.05f, 0.0f, 100000.0f, "%.2f m3/s", ImGuiSliderFlags_AlwaysClamp)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Stream 0.05-0.5, creek 1-5, river 20-100, large river 500+");
                _live_float(lw != nullptr, lw ? lw->flow : 0.0f, " m3/s");
            }
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<WaterSourceComponent>(e);
    }

    if (CollisionComponent* col = world.try_get<CollisionComponent>(e)) {
        bool remove = false;
        const CollisionComponent* lc = has_live ? live->try_get<CollisionComponent>(le) : nullptr;
        if (_section("Collision", &remove, overridden(collision_index) ? &revert_collision : nullptr)) {
            bool changed = false;
            static const char* SOURCES[] = { "Mesh", "Shapes" };
            int source = (int)col->source;
            _name("Source");
            if (ImGui::Combo("##source", &source, SOURCES, (int)CollisionSource::COUNT)) {
                col->source = (CollisionSource)source;
                if (col->source == CollisionSource::SHAPES && col->shape_count == 0) col->shape_count = 1;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Mesh: the collision built in the mesh's tab. Shapes: this entity's own shapes, for triggers and meshless things.");
            if (lc) _live("%s", SOURCES[ImClamp((int)lc->source, 0, 1)]);
            else _live("-");
            if (col->source == CollisionSource::MESH) {
                _name("Mesh Collision");
                const char* status = _mesh_collision_status(ctx, world, e);
                if (status) ImGui::TextDisabled("%s", status);
                else ImGui::TextColored(ui_vec4(ui.color.danger), "%s", world.has<ModelComponent>(e) ? "Mesh has no collision" : "Needs a Model component");
                _live("-");
            }
            static const char* MOTIONS[] = { "Static", "Kinematic", "Dynamic" };
            int motion = (int)col->motion;
            _name("Motion");
            if (ImGui::Combo("##motion", &motion, MOTIONS, 3)) {
                col->motion = (PhysicsMotion)motion;
                changed = true;
            }
            if (lc) _live("%s", MOTIONS[ImClamp((int)lc->motion, 0, 2)]);
            else _live("-");
            if (col->motion == PhysicsMotion::DYNAMIC) {
                _name("Mass");
                changed |= ImGui::DragFloat("##mass", &col->mass, 0.1f, 0.01f, 100000.0f, "%.2f kg");
                _live_float(lc != nullptr, lc ? lc->mass : 0.0f, " kg");
            }
            _name("Friction");
            changed |= ImGui::SliderFloat("##friction", &col->friction, 0.0f, 1.0f, "%.2f");
            _live_float(lc != nullptr, lc ? lc->friction : 0.0f);
            _name("Bounciness");
            changed |= ImGui::SliderFloat("##bounciness", &col->restitution, 0.0f, 1.0f, "%.2f");
            _live_float(lc != nullptr, lc ? lc->restitution : 0.0f);
            _name("Trigger");
            changed |= _check("##trigger", &col->trigger);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Detects overlaps without blocking anything");
            if (lc) _live("%s", lc->trigger ? "True" : "False");
            else _live("-");

            if (col->source == CollisionSource::SHAPES) {
                col->shape_count = std::min(col->shape_count, COLLISION_MAX_SHAPES);
                int32_t shape_sel = ctx.selection ? ctx.selection->shape() : -1;
                if (shape_sel >= (int32_t)col->shape_count) shape_sel = -1;
                uint32_t remove_shape = UINT32_MAX;
                _name("Shapes");
                ImGui::BeginDisabled(col->shape_count >= COLLISION_MAX_SHAPES);
                if (ImGui::BeginCombo("##add_shape", ICON_FA_PLUS "  Add Shape", ImGuiComboFlags_NoArrowButton)) {
                    for (int t = 0; t < (int)CollisionShapeType::COUNT; t++) {
                        if (!ImGui::Selectable(SHAPE_NAMES[t])) continue;
                        CollisionShapeEntry added;
                        added.type = (CollisionShapeType)t;
                        col->shapes[col->shape_count++] = added;
                        shape_sel = (int32_t)col->shape_count - 1;
                        changed = true;
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                _live("-");
                for (uint32_t i = 0; i < col->shape_count; i++) {
                    ImGui::PushID((int)i);
                    char shape_name[32];
                    std::snprintf(shape_name, sizeof(shape_name), "    %s", SHAPE_NAMES[(int)col->shapes[i].type]);
                    _name(shape_name);
                    if (ImGui::Selectable(shape_sel == (int32_t)i ? "Selected##shape" : "Select##shape", shape_sel == (int32_t)i, 0, ImVec2(ImMax(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x), 0.0f))) shape_sel = (int32_t)i;
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK "##remove_shape")) remove_shape = i;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Remove this shape");
                    _live("-");
                    ImGui::PopID();
                }
                if (remove_shape != UINT32_MAX) {
                    for (uint32_t i = remove_shape; i + 1 < col->shape_count; i++) col->shapes[i] = col->shapes[i + 1];
                    col->shapes[col->shape_count - 1] = CollisionShapeEntry{};
                    col->shape_count--;
                    if (shape_sel == (int32_t)remove_shape) shape_sel = -1;
                    else if (shape_sel > (int32_t)remove_shape) shape_sel--;
                    changed = true;
                }
                if (shape_sel >= 0) {
                    CollisionShapeEntry& s = col->shapes[shape_sel];
                    int type = (int)s.type;
                    _name("Type");
                    if (ImGui::Combo("##type", &type, SHAPE_NAMES, (int)CollisionShapeType::COUNT)) {
                        s.type = (CollisionShapeType)type;
                        changed = true;
                    }
                    _live("-");
                    const ShapeFields fields = shape_fields(s.type);
                    _name("Position");
                    changed |= ImGui::DragFloat3("##shape_position", &s.position.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
                    _live("-");
                    if (fields.rotation) {
                        _name("Rotation");
                        changed |= ImGui::DragFloat3("##shape_rotation", &s.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
                        _live("-");
                    }
                    if (fields.half_extents) {
                        _name("Half Extents");
                        changed |= ImGui::DragFloat3("##half_extents", &s.half_extents.x, 0.01f, 0.001f, 1000.0f, "%.2f");
                        _live("-");
                    }
                    if (fields.radius) {
                        _name(fields.top_radius ? "Bottom Radius" : "Radius");
                        changed |= ImGui::DragFloat("##shape_radius", &s.radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        _live("-");
                    }
                    if (fields.top_radius) {
                        _name("Top Radius");
                        changed |= ImGui::DragFloat("##top_radius", &s.top_radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        _live("-");
                    }
                    if (fields.half_height) {
                        _name("Half Height");
                        changed |= ImGui::DragFloat("##half_height", &s.half_height, 0.01f, 0.001f, 1000.0f, "%.2f");
                        _live("-");
                    }
                }
                if (ctx.selection && shape_sel != ctx.selection->shape()) ctx.selection->select_shape(shape_sel);
            }
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<CollisionComponent>(e);
    }

    _section_end();

    bool addable = false;
    for (uint32_t i = 0; i < (uint32_t)world.components.size() && !addable; i++) addable = world.components[i].addable && !world.components[i].has(world, e);
    ImGui::BeginDisabled(!addable);
    if (ui_list_add_begin("##add_component", "Add Component")) {
        for (uint32_t i = 0; i < (uint32_t)world.components.size(); i++) {
            const ComponentInfo& info = world.components[i];
            if (!info.addable || info.has(world, e)) continue;
            if (ImGui::Selectable(info.name)) world.deferred_add_component(e, i);
        }
        ui_list_add_end();
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ui_field_pop_style();

    if (tmpl && revert_transform) {
        template_revert_component(world, e, *tmpl, transform_index);
        template_revert_component(world, e, *tmpl, static_index);
        template_revert_component(world, e, *tmpl, disabled_index);
    }
    if (tmpl && revert_visible) template_revert_component(world, e, *tmpl, disabled_index);
    if (tmpl && revert_static) template_revert_component(world, e, *tmpl, static_index);
    if (tmpl && revert_model) template_revert_component(world, e, *tmpl, model_index);
    if (tmpl && revert_collision) template_revert_component(world, e, *tmpl, collision_index);
    if (tmpl && revert_character) template_revert_component(world, e, *tmpl, character_index);
    if (tmpl && revert_orbit) template_revert_component(world, e, *tmpl, orbit_index);
    if (tmpl && revert_water) template_revert_component(world, e, *tmpl, water_index);
    if (revert_entity) editor_revert_template(ctx, e);
    if (unpack) editor_unpack_template(ctx, e);
}

}
