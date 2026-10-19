#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <drivers/imgui/imgui_helpers.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

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

void ModelDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;

    if (imgui_category("Mesh")) {
        const AssetRegistry::Entry* mesh = ctx.assets ? ctx.assets->find(tab->model.mesh) : nullptr;
        asset_field(ctx, "###model_mesh", tab->model.mesh, AssetType::MESH, "Drop a mesh asset here");
        
        const LMeshStats& s = tab->mesh_stats;
        char buf[32];
        ImGui::Text("Name: %s", mesh ? mesh->name.c_str() : "None");
        ImGui::Text("Path: %s", mesh ? mesh->asset_path.c_str() : "-");
        if (s.disk_size >= 1024ull * 1024ull) ImGui::Text("Disk Size: %.2f MB", (double)s.disk_size / (1024.0 * 1024.0));
        else ImGui::Text("Disk Size: %.2f KB", (double)s.disk_size / 1024.0);
        ImGui::Text("Vertices: %s", _count(s.vertices, buf, sizeof(buf)));
        ImGui::Text("Triangles: %s", _count(s.triangles, buf, sizeof(buf)));
        ImGui::Text("Material Slots: %u", s.slots);
        ImGui::Text("Clusters: %s", _count(s.clusters, buf, sizeof(buf)));
        ImGui::Text("Cluster Vertices: %s", _count(s.cluster_vertices, buf, sizeof(buf)));
        ImGui::Text("Cluster Triangles: %s", _count(s.cluster_triangles, buf, sizeof(buf)));
        ImGui::Text("LODs: %u", s.lods);
        ImGui::Text("Approx Size: %.2fm x %.2fm x %.2fm", s.size.x, s.size.y, s.size.z);
    }

    if (imgui_category("Skeleton")) {
        
    }

    if (imgui_category("Materials")) {
        std::vector<Guid>& mats = tab->model.materials;
        const uint32_t count = std::max<uint32_t>(tab->mesh_stats.slots, (uint32_t)mats.size());
        if (count == 0) ImGui::TextDisabled("No material slots");
        bool changed = false;
        for (uint32_t i = 0; i < count; ++i) {
            char id[32];
            std::snprintf(id, sizeof(id), "###model_material_%u", i);
            ImGui::Text(i < tab->mesh_stats.slots ? "Slot %u" : "Slot %u (unused)", i);
            Guid m = i < mats.size() ? mats[i] : Guid{};
            if (asset_field(ctx, id, m, AssetType::MATERIAL, "Drop a material asset here (empty = default)")) {
                if (i >= mats.size()) mats.resize(i + 1);
                mats[i] = m;
                changed = true;
            }
        }
        while (changed && !mats.empty() && mats.back() == Guid{}) mats.pop_back();
    }
}

}