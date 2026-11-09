#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace lumen {

void ModelDetailsPanel::_draw_materials(EditorContext& ctx)
{
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

void ModelDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    (void)ctx;
    const LModel& m = tab->model;
    const uint32_t slots = tab->mesh_stats.slots;
    uint32_t empty = 0;
    for (uint32_t i = 0; i < slots; ++i) {
        if (i >= m.materials.size() || !m.materials[i].valid()) empty++;
    }
    const bool unused = m.materials.size() > slots && m.mesh.valid();
    if (m.mesh.valid() && !empty && !unused) return;

    if (!m.mesh.valid()) ui_warning("No mesh assigned");
    if (empty) ui_warning("%u of %u material slots are empty", empty, slots);
    if (unused) ui_warning("Materials past slot %u aren't used by this mesh", slots - 1);
}

void ModelDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LModel& m = tab->model;
    ui_field_push_style();

    if (ui_category("Model")) {
        asset_field(ctx, "Mesh", "###model_mesh", m.mesh, AssetType::MESH);
    }

    char header[48];
    std::snprintf(header, sizeof(header), "Materials (%u)###materials", std::max<uint32_t>(tab->mesh_stats.slots, (uint32_t)m.materials.size()));
    if (ui_category(header)) _draw_materials(ctx);

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
