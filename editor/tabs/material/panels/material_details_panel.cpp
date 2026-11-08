#include <editor/tabs/material/panels/material_details_panel.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <core/rendering/renderer.h>
#include <imgui.h>

namespace lumen {

void MaterialDetailsPanel::_draw_warnings(EditorContext& )
{
    LMaterial& m = tab->material;

    if (m.alpha_mode == AlphaMode::MASKED && !m.mask_texture.valid()) {
        ui_warning("Masked with no mask texture: renders as opaque");
    }
}

void MaterialDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LMaterial& m = tab->material;
    bool changed = false;
    ui_field_push_style();

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
        if (ui_category("Masking")) {
            changed |= asset_field(ctx, "Mask Texture", "###mat_mask_tex", m.mask_texture, AssetType::TEXTURE);
            changed |= ImGui::SliderFloat("Alpha Cutoff", &m.alpha_cutoff, 0.0f, 1.0f);
        }
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
    
    if (changed && ctx.renderer) ctx.renderer->materials.set(m);
}

}
