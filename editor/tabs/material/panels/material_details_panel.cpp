#include <editor/tabs/material/panels/material_details_panel.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/assets/asset_field.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <imgui.h>
#include <cfloat>

namespace lumen {

static bool _texture(EditorContext& ctx, const char* p_label, const char* p_id, Guid& r_guid)
{
    ImGui::TextUnformatted(p_label);
    return asset_thumbnail_field(ctx, p_id, r_guid, AssetType::TEXTURE, "Drop a texture asset here");
}

void MaterialDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LMaterial& m = tab->material;
    bool changed = false;

    if (imgui_category("Surface")) {

        int model = (int)m.shading_model;
        if (ImGui::BeginCombo("Shading Model", shading_model_name(m.shading_model))) {
            for (int i = (int)ShadingModel::SKY + 1; i <= (int)ShadingModel::EYE; ++i) {
                if (ImGui::Selectable(shading_model_name((ShadingModel)i), i == model)) {
                    m.shading_model = (ShadingModel)i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        int alpha = (int)m.alpha_mode;
        if (ImGui::BeginCombo("Alpha Mode", alpha_mode_name(m.alpha_mode))) {
            for (int i = (int)AlphaMode::NONE; i <= (int)AlphaMode::MASKED; ++i) {
                if (ImGui::Selectable(alpha_mode_name((AlphaMode)i), i == alpha)) {
                    m.alpha_mode = (AlphaMode)i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        if (alpha == (int)AlphaMode::MASKED) changed |= ImGui::SliderFloat("Alpha Cutoff", &m.alpha_cutoff, 0.0f, 1.0f);
        changed |= ImGui::Checkbox("Two Sided", &m.two_sided);
    }
    
    if (imgui_category("Color")) {
        changed |= ImGui::ColorEdit4("Base Color", &m.base_color.x);
        changed |= _texture(ctx, "Base Color Texture", "###mat_base_color_tex", m.base_color_texture);
    }
    
    if (imgui_category("Normal")) {
        changed |= _texture(ctx, "Normal Texture", "###mat_normal_tex", m.normal_texture);
        // if (m.normal_texture != Guid::INVALID) changed |= ImGui::DragFloat("Normal Scale", &m.normal_scale, 0.01f, 0.0f, 10.0f);
    }
    
    if (imgui_category("Roughness / Metallic / AO")) {
        changed |= ImGui::SliderFloat("Roughness", &m.roughness, 0.0f, 1.0f);
        changed |= _texture(ctx, "Roughness Texture", "###mat_roughness_tex", m.roughness_texture);
        changed |= ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
        changed |= _texture(ctx, "Metallic Texture", "###mat_metallic_tex", m.metallic_texture);
        changed |= ImGui::SliderFloat("AO Strength", &m.ao_strength, 0.0f, 1.0f);
        changed |= _texture(ctx, "AO Texture", "###mat_ao_tex", m.ao_texture);
    }
    
    if (imgui_category("Emmisive")) {
        changed |= ImGui::ColorEdit3("Emissive Color", &m.emissive_color.x);
        changed |= ImGui::DragFloat("Emissive Nits", &m.emissive_nits, 1.0f, 0.0f, FLT_MAX);
        changed |= _texture(ctx, "Emissive Texture", "###mat_emissive_tex", m.emissive_texture);
    }
    
    if (imgui_category("UVs")) {
        changed |= ImGui::DragFloat2("UV Scale", &m.uv_scale.x, 0.01f);
        changed |= ImGui::DragFloat2("UV Offset", &m.uv_offset.x, 0.01f);
    }
    
    // if (imgui_category("Misc")) {    
    //     ImGui::SeparatorText("Custom");
    //     changed |= ImGui::DragFloat4("Custom", &m.custom.x, 0.01f);
    //     changed |= _texture(ctx, "Custom Texture", "###mat_custom_tex", m.custom_texture);

    // }
    
    if (changed && ctx.renderer) ctx.renderer->materials.set(m);
}

}