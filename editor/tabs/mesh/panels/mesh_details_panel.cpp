#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <editor/tabs/mesh/mesh_tab.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <editor/assets/import/import_dialog.h>
#include <editor/assets/import/collision_cooker.h>
#include <drivers/windows/dialogs_win32.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>

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
    if (rig.skinned && ui_category("Skeleton")) {
        asset_field(ctx, "Skeleton", "###mesh_skeleton", rig.skeleton, AssetType::SKELETON);
    }

    if (ui_category("Collision")) {
        MeshTab::CollisionView& c = tab->collision;
        const bool locked = tab->locked(ctx);
        if (locked) ImGui::BeginDisabled();

        if (!c.generate || !c.has) {
            ImGui::TextDisabled(!c.generate ? "Generate Collision is off for this mesh." : "This mesh has no collision yet.");
            collision_settings_fields(c.edit);
            if (ImGui::Button(ICON_FA_DRAW_POLYGON "  Generate Collision")) CollisionCooker::reimport(ctx, tab->path, c.edit);
        } else {
            if (c.loaded) {
                const LCollision& d = c.data;
                const vec3 size = d.bounds_max - d.bounds_min;
                ImGui::Text("Type: %s", collision_type_label(d.type));
                if (!d.primitive()) {
                    ImGui::Text("Triangles: %s", fmt_count(d.triangle_count()));
                    ImGui::Text("Source Triangles: %s", fmt_count(d.source_triangles));
                    ImGui::Text("Vertices: %s", fmt_count((uint32_t)d.vertices.size()));
                }
                ImGui::Text("Size: %.2fm x %.2fm x %.2fm", size.x, size.y, size.z);
            } else {
                ImGui::TextDisabled("Collision data couldn't be read.");
            }
            ImGui::Spacing();

            collision_settings_fields(c.edit);
            const bool changed = !(c.edit == c.settings);
            if (!changed) ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_CHECK "  Apply")) CollisionCooker::reimport(ctx, tab->path, c.edit);
            ImGui::SameLine();
            if (ImGui::Button("Revert")) c.edit = c.settings;
            if (!changed) ImGui::EndDisabled();

            ImGui::Spacing();
            ui_push_buttons(UiButton::DANGER);
            const bool remove = ImGui::Button(ICON_FA_TRASH_CAN "  Delete Collision");
            ui_pop_buttons();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Removes the collision and turns Generate Collision off for this mesh");
            if (remove && drivers::Win32Dialogs::ask(L"Lumen Editor", L"Delete this mesh's collision?", L"Generate Collision is turned off so reimports won't bring it back. You can generate it again from here.", L"Delete", L"Cancel")) {
                CollisionCooker::remove(ctx, tab->path);
            }
        }

        if (locked) ImGui::EndDisabled();
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
