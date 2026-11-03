#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <editor/tabs/mesh/mesh_tab.h>
#include <core/rendering/renderer.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

static const char* _count(uint64_t p_value, char* r_buf, size_t p_size)
{
    char raw[32];
    const int n = std::snprintf(raw, sizeof(raw), "%llu", (unsigned long long)p_value);
    size_t o = 0;
    for (int i = 0; i < n && o + 1 < p_size; ++i) {
        if (i > 0 && (n - i) % 3 == 0 && o + 1 < p_size) r_buf[o++] = ',';
        r_buf[o++] = raw[i];
    }
    r_buf[o] = '\0';
    return r_buf;
}

void MeshDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;

    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(tab->guid) : nullptr;
    ImGui::Text("Name: %s", e ? e->name.c_str() : "None");
    ImGui::Text("Path: %s", e ? e->asset_path.c_str() : "-");
    if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
    if (!tab->source.empty()) {
        ImGui::TextUnformatted("Source:");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tab->source.c_str());
        ImGui::PopTextWrapPos();
    }

    RigPreview& rig = tab->rig_preview;
    if (rig.skinned) {
        asset_field(ctx, "Skeleton", "###mesh_skeleton", rig.skeleton, AssetType::SKELETON);
        if (rig.missing_joints) ImGui::TextColored(ui_vec4(ui_style().color.warning), "%u of %u joints aren't in this skeleton", rig.missing_joints, (uint32_t)rig.rig.joints.size());
    }

    const LMeshStats& s = tab->mesh_stats;
    char buf[32];
    ImGui::Text("Vertices: %s", _count(s.vertices, buf, sizeof(buf)));
    ImGui::Text("Triangles: %s", _count(s.triangles, buf, sizeof(buf)));
    ImGui::Text("Material Slots: %u", s.slots);
    if (s.joints) ImGui::Text("Joints: %u", s.joints);
    ImGui::Text("Clusters: %s", _count(s.clusters, buf, sizeof(buf)));
    ImGui::Text("Cluster Vertices: %s", _count(s.cluster_vertices, buf, sizeof(buf)));
    ImGui::Text("Cluster Triangles: %s", _count(s.cluster_triangles, buf, sizeof(buf)));
    ImGui::Text("LODs: %u", s.lods);
    ImGui::Text("Approx Size: %.2fm x %.2fm x %.2fm", s.size.x, s.size.y, s.size.z);
}

}
