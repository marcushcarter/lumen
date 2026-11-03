#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/tabs/model/model_tab.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace lumen {

void ModelDetailsPanel::draw_contents(EditorContext& ctx)
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

    ImGui::Text("Triangles: %llu", (unsigned long long)tab->mesh_stats.triangles);
    ImGui::Text("Material Slots: %u", tab->mesh_stats.slots);

    if (ui_category("Mesh")) {
        asset_field(ctx, "Mesh", "###model_mesh", tab->model.mesh, AssetType::MESH);
    }

    if (ui_category("Skeleton")) {

    }

    if (ui_category("Materials")) {
        std::vector<Guid>& mats = tab->model.materials;
        const uint32_t count = std::max<uint32_t>(tab->mesh_stats.slots, (uint32_t)mats.size());
        if (count == 0) ImGui::TextDisabled("No material slots");
        bool changed = false;
        for (uint32_t i = 0; i < count; ++i) {
            char id[32];
            std::snprintf(id, sizeof(id), "###model_material_%u", i);
            char slot[32];
            std::snprintf(slot, sizeof(slot), i < tab->mesh_stats.slots ? "Slot %u" : "Slot %u (unused)", i);
            Guid m = i < mats.size() ? mats[i] : Guid{};
            if (asset_field(ctx, slot, id, m, AssetType::MATERIAL)) {
                if (i >= mats.size()) mats.resize(i + 1);
                mats[i] = m;
                changed = true;
            }
        }
        while (changed && !mats.empty() && !mats.back().valid()) mats.pop_back();
    }
}

}
