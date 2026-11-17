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

    RigPreview& rig = tab->rig_preview;
    if (!rig.skinned && ui_category("Collision")) {
        MeshDocument::CollisionView& c = tab->collision;
        const bool locked = tab->locked(ctx);
        if (locked) ImGui::BeginDisabled();

        static constexpr CollisionType MODES[] = { CollisionType::MESH, CollisionType::HULL, CollisionType::SHAPES };
        const char* current = collision_type_label(c.edit.type);
        if (ImGui::BeginCombo("Mode", current)) {
            for (CollisionType t : MODES) {
                if (!ImGui::Selectable(collision_type_label(t), c.edit.type == t) || (c.edit.type == t)) continue;
                c.edit.type = t;
                if (t == CollisionType::SHAPES && c.edit.shapes.empty()) tab->add_shape(CollisionType::BOX);
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Triangle Mesh is exact but static only. Convex Hull and Shapes work on moving objects.");

        if (tab->shapes_mode()) {
            std::vector<CollisionShapeSettings>& shapes = c.edit.shapes;
            if (ImGui::BeginCombo("##add_shape", ICON_FA_PLUS "  Add Shape", ImGuiComboFlags_NoArrowButton)) {
                for (int i = 0; i < (int)CollisionShapeType::COUNT; i++) if (ImGui::Selectable(SHAPE_NAMES[i])) tab->add_shape(shape_collision_type((CollisionShapeType)i));
                ImGui::EndCombo();
            }
            int32_t remove = -1;
            for (int32_t i = 0; i < (int32_t)shapes.size(); i++) {
                ImGui::PushID(i);
                char label[160];
                std::snprintf(label, sizeof(label), "%s  %s", SHAPE_NAMES[(int)shape_kind(shapes[i].type)], shapes[i].name.c_str());
                if (ImGui::Selectable(label, tab->selected_shape == i, 0, ImVec2(ImMax(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x), 0.0f))) tab->selected_shape = i;
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK)) remove = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Remove this shape");
                ImGui::PopID();
            }
            if (remove >= 0) {
                shapes.erase(shapes.begin() + remove);
                if (tab->selected_shape == remove) tab->selected_shape = -1;
                else if (tab->selected_shape > remove) tab->selected_shape--;
            }

            const int32_t sel = tab->selected_shape;
            if (sel >= 0 && sel < (int32_t)shapes.size()) {
                CollisionShapeSettings& s = shapes[sel];
                ImGui::Spacing();
                int type = (int)shape_kind(s.type);
                if (ImGui::Combo("Type", &type, SHAPE_NAMES, (int)CollisionShapeType::COUNT)) s.type = shape_collision_type((CollisionShapeType)type);
                char name[64];
                std::snprintf(name, sizeof(name), "%s", s.name.c_str());
                if (ImGui::InputText("Name", name, sizeof(name))) s.name = name;
                const ShapeFields fields = shape_fields(shape_kind(s.type));
                ImGui::DragFloat3("Position", &s.position.x, 0.01f, -1000.0f, 1000.0f, "%.2f");
                if (fields.rotation) ImGui::DragFloat3("Rotation", &s.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
                if (fields.half_extents) ImGui::DragFloat3("Half Extents", &s.half_extents.x, 0.01f, 0.001f, 1000.0f, "%.2f");
                if (fields.radius) ImGui::DragFloat(fields.top_radius ? "Bottom Radius" : "Radius", &s.radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                if (fields.top_radius) ImGui::DragFloat("Top Radius", &s.top_radius, 0.01f, 0.001f, 1000.0f, "%.2f");
                if (fields.half_height) ImGui::DragFloat("Half Height", &s.half_height, 0.01f, 0.001f, 1000.0f, "%.2f");
                if (ImGui::Button(ICON_FA_EXPAND "  Fit to Mesh")) tab->fit_shape(s);
            }
            if (tab->shapes_dirty()) ImGui::TextDisabled("Save (Ctrl+S) to cook these shapes into the mesh.");
        } else {
            collision_settings_fields(c.edit);
            if (c.loaded && c.edit.type == c.data.type) {
                const LCollision& d = c.data;
                const vec3 size = d.bounds_max - d.bounds_min;
                ImGui::Text("Triangles: %s", fmt_count(d.triangle_count()));
                ImGui::Text("Vertices: %s", fmt_count((uint32_t)d.vertices.size()));
                ImGui::Text("Size: %.2fm x %.2fm x %.2fm", size.x, size.y, size.z);
            }
            const bool changed = !(c.edit == c.settings) || !c.has;
            if (!changed) ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_DRAW_POLYGON "  Generate")) CollisionCooker::reimport(ctx, tab->path, c.edit);
            ImGui::SameLine();
            if (ImGui::Button("Revert")) c.edit = c.settings;
            if (!changed) ImGui::EndDisabled();
        }

        if (locked) ImGui::EndDisabled();
    }

    if (rig.skinned && ui_category("Dynamics")) {
        const LSkeleton& skeleton = rig.skeleton_data();
        const bool locked = tab->locked(ctx);
        if (locked) ImGui::BeginDisabled();
        bool changed = false;
        int32_t& bone = tab->selected_bone;
        const bool valid = bone >= 0 && bone < (int32_t)skeleton.joint_count();
        ui_property_row("Bone", "%s", valid ? skeleton.names[bone].c_str() : "Click a bone in the viewport");

        int32_t chain_at = -1, cloth_at = -1;
        for (size_t i = 0; valid && i < tab->chains.size(); i++) if (tab->chains[i].root == bone) chain_at = (int32_t)i;
        for (size_t i = 0; valid && i < tab->cloths.size(); i++) if (tab->cloths[i].root == bone) cloth_at = (int32_t)i;

        ImGui::BeginDisabled(!valid || chain_at >= 0 || cloth_at >= 0);
        if (ImGui::Button(ICON_FA_LINK "  Add Chain")) {
            SkeletonChain c;
            c.root = bone;
            tab->chains.push_back(c);
            chain_at = (int32_t)tab->chains.size() - 1;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SHIRT "  Add Cloth")) {
            SkeletonCloth c;
            c.root = bone;
            tab->cloths.push_back(c);
            cloth_at = (int32_t)tab->cloths.size() - 1;
            changed = true;
        }
        ImGui::EndDisabled();

        int32_t remove_chain = -1, remove_cloth = -1;
        for (int32_t i = 0; i < (int32_t)tab->chains.size(); i++) {
            ImGui::PushID(i);
            char label[160];
            std::snprintf(label, sizeof(label), ICON_FA_LINK "  %s", skeleton.names[tab->chains[i].root].c_str());
            if (ImGui::Selectable(label, i == chain_at, 0, ImVec2(ImMax(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x), 0.0f))) bone = tab->chains[i].root;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove_chain = i;
            ImGui::PopID();
        }
        for (int32_t i = 0; i < (int32_t)tab->cloths.size(); i++) {
            ImGui::PushID(1000 + i);
            char label[160];
            std::snprintf(label, sizeof(label), ICON_FA_SHIRT "  %s", skeleton.names[tab->cloths[i].root].c_str());
            if (ImGui::Selectable(label, i == cloth_at, 0, ImVec2(ImMax(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x), 0.0f))) bone = tab->cloths[i].root;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) remove_cloth = i;
            ImGui::PopID();
        }

        if (chain_at >= 0 && chain_at != remove_chain) {
            SkeletonChain& c = tab->chains[chain_at];
            ImGui::Spacing();
            changed |= ImGui::SliderFloat("Stiffness", &c.stiffness, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Damping", &c.damping, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Inertia", &c.inertia, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::DragFloat("Radius", &c.radius, 0.001f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Angle Limit", &c.angle_limit, 0.0f, 180.0f, "%.0f deg");
        }
        if (cloth_at >= 0 && cloth_at != remove_cloth) {
            SkeletonCloth& c = tab->cloths[cloth_at];
            ImGui::Spacing();
            int resolution = (int)c.resolution;
            if (ImGui::SliderInt("Resolution", &resolution, 4, 256)) {
                c.resolution = (uint32_t)resolution;
                changed = true;
            }
            changed |= ImGui::DragFloat("Max Distance", &c.max_distance, 0.01f, 0.0f, 5.0f, "%.2f");
            changed |= ImGui::SliderFloat("Stretch", &c.stretch, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Bend", &c.bend, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Damping", &c.damping, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Inertia", &c.inertia, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::DragFloat("Thickness", &c.thickness, 0.001f, 0.0f, 0.2f, "%.3f");
        }
        if (remove_chain >= 0) {
            tab->chains.erase(tab->chains.begin() + remove_chain);
            changed = true;
        }
        if (remove_cloth >= 0) {
            tab->cloths.erase(tab->cloths.begin() + remove_cloth);
            changed = true;
        }
        if (changed) tab->push_dynamics(ctx);
        if (tab->cloth_rebake()) ImGui::TextDisabled("Save (Ctrl+S) to rebuild the cloth.");
        else if (tab->dynamics_dirty()) ImGui::TextDisabled("Save (Ctrl+S) to keep these settings.");
        if (locked) ImGui::EndDisabled();
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
