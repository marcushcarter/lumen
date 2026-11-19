#include <editor/documents/mesh/panels/mesh_details_panel.h>
#include <editor/documents/mesh/mesh_document.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <editor/assets/import/import_dialog.h>
#include <editor/assets/import/collision_cooker.h>
#include <editor/viewport/shape_gizmo.h>
#include <drivers/windows/dialogs_win32.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace lumen {

void MeshDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    (void)ctx;
    const RigPreview& rig = tab->rig_preview;
    if (!rig.skinned || !rig.missing_joints) return;
    ui_warning("%u of %u joints aren't in this skeleton", rig.missing_joints, (uint32_t)rig.rig.joints.size());
}

void MeshDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    ui_field_push_style();

    tab->draw_import();

    if (ui_category("Info")) {
        const LMeshStats& s = tab->mesh_stats;
        if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
        ImGui::Text("Vertices: %s", fmt_count(s.vertices));
        ImGui::Text("Triangles: %s", fmt_count(s.triangles));
        ImGui::Text("Material Slots: %u", s.slots);
        if (s.joints) ImGui::Text("Joints: %u", s.joints);
        ImGui::Text("Clusters: %s", fmt_count(s.clusters));
        ImGui::Text("Cluster Vertices: %s", fmt_count(s.cluster_vertices));
        ImGui::Text("Cluster Triangles: %s", fmt_count(s.cluster_triangles));
        ImGui::Text("LODs: %u", s.lods);
        ImGui::Text("Size: %.2fm x %.2fm x %.2fm", s.size.x, s.size.y, s.size.z);
    }

    if (ui_category("Collision")) {
        MeshDocument::CollisionView& c = tab->collision;
        const bool locked = tab->locked(ctx);
        if (locked) ImGui::BeginDisabled();

        static constexpr float CM_PER_M = 100.0f;
        std::vector<CollisionShapeSettings>& shapes = c.edit.shapes;
        const uint32_t slot_count = tab->mesh_stats.slots;
        const bool dirty = tab->shapes_dirty();
        const auto source_label = [](int32_t p_slot, char* r_buf, size_t p_size) {
            if (p_slot < 0) std::snprintf(r_buf, p_size, "All slots");
            else std::snprintf(r_buf, p_size, "Slot %d", p_slot);
        };
        int32_t generated_total = 0;
        for (const CollisionShapeSettings& s : shapes) generated_total += s.generated() ? 1 : 0;
        const bool parts_match = c.loaded && !dirty && generated_total == (int32_t)c.data.parts.size();

        int32_t remove = -1;
        int32_t generated_at = 0;
        for (int32_t i = 0; i < (int32_t)shapes.size(); i++) {
            CollisionShapeSettings& s = shapes[i];
            const bool generated = s.generated();
            const CollisionShapeType kind = shape_kind(s.type);
            const bool open = tab->selected_shape == i;
            bool removed = false;
            char summary[64];
            if (generated) {
                char source[32];
                source_label(s.slot, source, sizeof(source));
                std::snprintf(summary, sizeof(summary), "%s, %s", collision_type_label(s.type), source);
            } else {
                std::snprintf(summary, sizeof(summary), "%s", SHAPE_NAMES[(int)kind]);
            }
            const char* icon = s.type == CollisionType::MESH ? ICON_FA_DRAW_POLYGON : s.type == CollisionType::HULL ? ICON_FA_GEM : ICON_FA_SHAPES;
            ImGui::PushID(i);
            if (ui_list_item("##shape", icon, s.name.empty() ? collision_type_label(s.type) : s.name.c_str(), summary, open, &removed)) tab->selected_shape = open ? -1 : i;
            if (removed) remove = i;
            if (open && !removed) {
                ui_list_body_begin();
                if (ui_property_grid_begin("##shape_fields")) {
                    if (generated) {
                        static constexpr CollisionType GENERATED[] = { CollisionType::MESH, CollisionType::HULL };
                        ui_property("Type");
                        if (ImGui::BeginCombo("##type", collision_type_label(s.type))) {
                            for (CollisionType t : GENERATED) {
                                const bool taken = t != s.type && tab->source_used(t, s.slot, i);
                                if (ImGui::Selectable(collision_type_label(t), t == s.type, taken ? ImGuiSelectableFlags_Disabled : 0)) s.type = t;
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Triangle Mesh is exact but only works on objects that never move. Convex Hull wraps the triangles tightly and works on moving objects.");
                        ui_property("Name");
                        char name[64];
                        std::snprintf(name, sizeof(name), "%s", s.name.c_str());
                        if (ImGui::InputText("##name", name, sizeof(name))) s.name = name;
                        ui_property("Source");
                        char source[32];
                        source_label(s.slot, source, sizeof(source));
                        if (ImGui::BeginCombo("##source", source)) {
                            for (int32_t slot = COLLISION_ALL_SLOTS; slot < (int32_t)slot_count; slot++) {
                                char item[32];
                                source_label(slot, item, sizeof(item));
                                const bool taken = slot != s.slot && tab->source_used(s.type, slot, i);
                                if (ImGui::Selectable(item, slot == s.slot, taken ? ImGuiSelectableFlags_Disabled : 0)) s.slot = slot;
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Which material slot's triangles this is built from. Cloth slots are always left out.");
                        ui_property("Max Error");
                        float error_cm = s.error * CM_PER_M;
                        if (ImGui::DragFloat("##error", &error_cm, 0.1f, 0.0f, 100.0f, "%.1f cm", ImGuiSliderFlags_AlwaysClamp)) s.error = error_cm / CM_PER_M;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far the collision may stray from the real surface. 0 keeps every triangle.");
                        ui_property("Min Feature Size");
                        float feature_cm = s.min_feature * CM_PER_M;
                        if (ImGui::DragFloat("##feature", &feature_cm, 0.5f, 0.0f, 500.0f, "%.1f cm", ImGuiSliderFlags_AlwaysClamp)) s.min_feature = feature_cm / CM_PER_M;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Separate pieces smaller than this are left out. The largest piece is always kept.");
                        ui_property("Skip Masked");
                        ImGui::Checkbox("##masked", &s.exclude_masked);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Leaves out alpha-masked and transparent materials, like leaves and grass cards");
                        if (parts_match) {
                            const LCollisionPart& part = c.data.parts[generated_at];
                            ui_property("Triangles");
                            ImGui::TextDisabled("%s", fmt_count(part.index_count / 3));
                        }
                    } else {
                        const ShapeFields fields = shape_fields(kind);
                        ui_property("Type");
                        int type = (int)kind;
                        if (ImGui::Combo("##type", &type, SHAPE_NAMES, (int)CollisionShapeType::COUNT)) s.type = shape_collision_type((CollisionShapeType)type);
                        ui_property("Name");
                        char name[64];
                        std::snprintf(name, sizeof(name), "%s", s.name.c_str());
                        if (ImGui::InputText("##name", name, sizeof(name))) s.name = name;
                        ui_property("Position");
                        ImGui::DragFloat3("##position", &s.position.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
                        if (fields.rotation) {
                            ui_property("Rotation");
                            ImGui::DragFloat3("##rotation", &s.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
                        }
                        if (fields.half_extents) {
                            ui_property("Half Extents");
                            ImGui::DragFloat3("##extents", &s.half_extents.x, 0.01f, 0.001f, 1000.0f, "%.2f");
                        }
                        if (fields.radius) {
                            ui_property(fields.top_radius ? "Bottom Radius" : "Radius");
                            ImGui::DragFloat("##radius", &s.radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        }
                        if (fields.top_radius) {
                            ui_property("Top Radius");
                            ImGui::DragFloat("##top_radius", &s.top_radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                        }
                        if (fields.half_height) {
                            ui_property("Half Height");
                            ImGui::DragFloat("##half_height", &s.half_height, 0.01f, 0.001f, 1000.0f, "%.2f");
                        }
                    }
                    ui_property_grid_end();
                }
                if (!generated && ImGui::Button(ICON_FA_EXPAND "  Fit to Mesh")) tab->fit_shape(s);
                ui_list_body_end();
            }
            ImGui::PopID();
            if (generated) generated_at++;
        }
        if (remove >= 0) {
            shapes.erase(shapes.begin() + remove);
            if (tab->selected_shape == remove) tab->selected_shape = -1;
            else if (tab->selected_shape > remove) tab->selected_shape--;
        }
        if (ui_list_add_begin("##add_shape", "Add Shape")) {
            static constexpr CollisionType GENERATED[] = { CollisionType::MESH, CollisionType::HULL };
            for (CollisionType t : GENERATED) {
                const bool full = tab->free_source(t) == INT32_MIN;
                if (ImGui::Selectable(collision_type_label(t), false, full ? ImGuiSelectableFlags_Disabled : 0)) tab->add_shape(t);
            }
            ImGui::Separator();
            for (int i = 0; i < (int)CollisionShapeType::COUNT; i++) if (ImGui::Selectable(SHAPE_NAMES[i])) tab->add_shape(shape_collision_type((CollisionShapeType)i));
            ui_list_add_end();
        }
        if (shapes.empty()) ImGui::TextDisabled("No collision. Add a shape to give this mesh collision.");
        if (dirty) ImGui::TextDisabled("Save (Ctrl+S) to cook the collision into the mesh.");

        if (locked) ImGui::EndDisabled();
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
