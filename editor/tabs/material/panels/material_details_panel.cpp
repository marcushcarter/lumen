#include <editor/tabs/material/panels/material_details_panel.h>
#include <editor/tabs/material/material_tab.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <core/rendering/renderer.h>
#include <imgui.h>

namespace lumen {

void MaterialDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;

    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(tab->guid) : nullptr;
    ImGui::Text("Name: %s", e ? e->name.c_str() : "None");
    ImGui::Text("Path: %s", e ? e->asset_path.c_str() : "-");
    if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));

    if (!ui_category("Surface", nullptr, true)) return;
    LMaterial& m = tab->material;
    bool changed = false;

    int alpha = (int)m.alpha_mode;
    if (ImGui::BeginCombo("Alpha Mode", alpha_mode_name(m.alpha_mode))) {
        for (int i = (int)AlphaMode::NONE; i <= (int)AlphaMode::BLEND; ++i) {
            if (ImGui::Selectable(alpha_mode_name((AlphaMode)i), i == alpha)) {
                m.alpha_mode = (AlphaMode)i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::Checkbox("Two Sided", &m.two_sided);

    if (m.alpha_mode == AlphaMode::MASKED) {
        changed |= asset_field(ctx, "Mask Texture", "###mat_mask_tex", m.mask_texture, AssetType::TEXTURE);
        changed |= ImGui::SliderFloat("Alpha Cutoff", &m.alpha_cutoff, 0.0f, 1.0f);
    }

    if (changed && ctx.renderer) ctx.renderer->materials.set(m);
}

}
