#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <editor/tabs/mesh/mesh_tab.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void MeshDetailsPanel::_draw_stats(EditorContext& ctx)
{
    (void)ctx;
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
    _draw_stats(ctx);

    RigPreview& rig = tab->rig_preview;
    if (rig.skinned) {
        ImGui::Spacing();
        asset_field(ctx, "Skeleton", "###mesh_skeleton", rig.skeleton, AssetType::SKELETON);
    }

    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
