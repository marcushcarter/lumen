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
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

static bool _section(const char* p_label, bool* r_remove = nullptr, bool* r_revert = nullptr)
{
    const UiStyle& ui = ui_style();
    const float row = ui.row;
    ImGui::TableNextRow(ImGuiTableRowFlags_None, row);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui.color.header);
    ImGui::TableSetColumnIndex(0);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(p_label);
    bool* open = storage->GetBoolRef(id, true);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui.color.transparent);
    ImGui::PushID(p_label);
    if (ImGui::Selectable("##section", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, row))) *open = !*open;
    ImGui::PopID();
    ImGui::PopStyleColor(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* icon = *open ? ICON_FA_SQUARE_MINUS : ICON_FA_SQUARE_PLUS;
    const float ty = ImFloor(p.y + (row - ImGui::GetFontSize()) * 0.5f);
    const float x = p.x + ui.space.sm;
    const ImRect clip = ImGui::GetCurrentWindow()->InnerClipRect;
    dl->PushClipRect(clip.Min, clip.Max, false);
    dl->AddText(ImVec2(x, ty), ui.color.text, icon);
    dl->AddText(ImVec2(x + ImGui::CalcTextSize(icon).x + ui.space.md, ty), ui.color.text, p_label);
    dl->PopClipRect();
    if (r_remove || r_revert) {
        ImGui::TableSetColumnIndex(2);
        const float bw = row;
        const float count = (r_remove ? 1.0f : 0.0f) + (r_revert ? 1.0f : 0.0f);
        const float x0 = ImGui::GetCursorScreenPos().x + ImMax(0.0f, ImGui::GetContentRegionAvail().x - bw * count);
        ImGui::PushID(p_label);
        ImGui::PushStyleColor(ImGuiCol_Button, ui.color.transparent);
        if (r_revert) {
            ImGui::SetCursorScreenPos(ImVec2(x0, p.y));
            *r_revert = ImGui::Button(ICON_FA_ROTATE_LEFT "##revert", ImVec2(bw, row));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Revert to the template's value");
        }
        if (r_remove) {
            ImGui::SetCursorScreenPos(ImVec2(x0 + (r_revert ? bw : 0.0f), p.y));
            *r_remove = ImGui::Button(ICON_FA_XMARK "##remove", ImVec2(bw, row));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Remove component");
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    return *open;
}

static void _name(const char* p_name)
{
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ui_style().row);
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p_name);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

static bool _check(const char* p_id, bool* r_value)
{
    const float pad = ImFloor(ui_style().space.xxs);
    const float box = ImGui::GetFontSize() + pad * 2.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor((ui_style().row - box) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
    const bool changed = ImGui::Checkbox(p_id, r_value);
    ImGui::PopStyleVar();
    return changed;
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
    c.status = settings.generate_collision && CollisionCooker::has_collision(entry->path) ? collision_type_label(settings.collision.type) : nullptr;
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
    const bool has_target = ctx.selection->environment || world.valid(selected);
    {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float row = ui.row;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + w, o.y + row), ui.color.header);
        const float col_w = (w - row) / 3.0f;
        const float ty = ImFloor(o.y + (row - ImGui::GetFontSize()) * 0.5f);
        static constexpr const char* HEADERS[3] = { "Name", "Value", "Live Value" };
        for (int i = 0; i < 3; i++) dl->AddText(ImVec2(ImFloor(o.x + col_w * (float)i + ui.space.sm), ty), ui.color.text, HEADERS[i]);
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

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
    const float footer_h = ctx.selection->environment ? 0.0f : ui.row;
    ui_field_push_style();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.space.sm, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    if (!ImGui::BeginTable("##properties", 3, flags, ImVec2(0.0f, -footer_h))) {
        ImGui::PopStyleVar(2);
        ui_field_pop_style();
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize, 1.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize, 1.0f);
    ImGui::TableSetupColumn("Live Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize, 1.0f);

    if (ctx.selection->environment) {
        EnvironmentSettings& env = world.environment;
        if (_section("Scene Info")) {
            _name("Exposure EV100");
            ImGui::DragFloat("##ev100", &env.manual_ev100, 0.05f, -6.0f, 18.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            _live_float(live != nullptr, live ? live->environment.manual_ev100 : 0.0f);
        }
        ImGui::EndTable();
        ImGui::PopStyleVar(2);
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
    const uint32_t locomotion_index = component("locomotion");
    const uint32_t player_index = component("player");
    const uint32_t static_index = component("static");

    bool revert_transform = false;
    bool revert_model = false;
    bool revert_static = false;
    bool revert_entity = false;
    bool revert_locomotion = false;
    bool revert_player = false;
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

    if (TransformComponent* xf = world.try_get<TransformComponent>(e)) {
        const TransformComponent* lxf = has_live ? live->try_get<TransformComponent>(le) : nullptr;
        if (_section("Transform", nullptr, overridden(transform_index) || overridden(static_index) ? &revert_transform : nullptr)) {
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
        }
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
        }
        if (remove) world.deferred_remove<ModelComponent>(e);
    }

    if (LocomotionComponent* loco = world.try_get<LocomotionComponent>(e)) {
        bool remove = false;
        const LocomotionComponent* ll = has_live ? live->try_get<LocomotionComponent>(le) : nullptr;
        if (_section("Locomotion", &remove, overridden(locomotion_index) ? &revert_locomotion : nullptr)) {
            bool changed = false;
            _name("Locomotion");
            changed |= asset_field(ctx, "", "###locomotion_asset", loco->locomotion, AssetType::LOCOMOTION);
            _live("-");
            _name("Height");
            changed |= ImGui::DragFloat("##height", &loco->height, 0.01f, 0.5f, 4.0f, "%.2f m");
            _live_float(ll != nullptr, ll ? ll->height : 0.0f, " m");
            _name("Crouch Height");
            changed |= ImGui::DragFloat("##crouch_height", &loco->crouch_height, 0.01f, 0.3f, loco->height, "%.2f m");
            _live_float(ll != nullptr, ll ? ll->crouch_height : 0.0f, " m");
            _name("Radius");
            changed |= ImGui::DragFloat("##radius", &loco->radius, 0.005f, 0.05f, 1.0f, "%.2f m");
            _live_float(ll != nullptr, ll ? ll->radius : 0.0f, " m");
            _name("Max Step Height");
            changed |= ImGui::DragFloat("##max_step", &loco->max_step_height, 0.005f, 0.0f, 1.0f, "%.2f m");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Tallest ledge or stair the character walks up without jumping");
            _live_float(ll != nullptr, ll ? ll->max_step_height : 0.0f, " m");
            _name("Max Slope");
            changed |= ImGui::DragFloat("##max_slope", &loco->max_slope, 0.5f, 1.0f, 89.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Steepest slope the character can walk up. Steeper ground makes it slide.");
            _live_float(ll != nullptr, ll ? ll->max_slope : 0.0f, " deg");
            _name("Model Yaw");
            changed |= ImGui::DragFloat("##model_yaw", &loco->model_yaw, 1.0f, -180.0f, 180.0f, "%.0f deg");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn this if the model faces the wrong way in game");
            _live_float(ll != nullptr, ll ? ll->model_yaw : 0.0f, " deg");
            if (changed) world.touch(e);
        }
        if (remove) world.deferred_remove<LocomotionComponent>(e);
    }

    if (world.has<PlayerTag>(e)) {
        bool remove = false;
        if (_section("Player", &remove, overridden(player_index) ? &revert_player : nullptr)) {
            _name("Input");
            ImGui::TextDisabled(world.has<LocomotionComponent>(e) ? "Gets the input and the camera in play" : "Needs a Locomotion component to move");
            _live(has_live ? "Active" : "-");
        }
        if (remove) world.deferred_remove<PlayerTag>(e);
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

    ImGui::EndTable();
    ImGui::PopStyleVar(2);
    ui_field_pop_style();

    if (tmpl && revert_transform) {
        template_revert_component(world, e, *tmpl, transform_index);
        template_revert_component(world, e, *tmpl, static_index);
    }
    if (tmpl && revert_static) template_revert_component(world, e, *tmpl, static_index);
    if (tmpl && revert_model) template_revert_component(world, e, *tmpl, model_index);
    if (tmpl && revert_collision) template_revert_component(world, e, *tmpl, collision_index);
    if (tmpl && revert_locomotion) template_revert_component(world, e, *tmpl, locomotion_index);
    if (tmpl && revert_player) template_revert_component(world, e, *tmpl, player_index);
    if (revert_entity) editor_revert_template(ctx, e);
    if (unpack) editor_unpack_template(ctx, e);

    ImGui::PushID("Add Component");
    const bool add_pressed = ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f));
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
