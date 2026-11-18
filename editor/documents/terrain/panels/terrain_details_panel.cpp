#include <editor/documents/terrain/panels/terrain_details_panel.h>
#include <editor/documents/terrain/terrain_document.h>
#include <core/base/strings.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <cstdio>
#include <iterator>

namespace lumen {

void TerrainDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LTerrain& t = tab->terrain;
    ui_field_push_style();

    tab->draw_import();

    if (ui_category("Info")) {
        if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
        if (tab->has_data()) {
            ImGui::Text("Size: %.2f x %.2f m", (float)(t.samples_x - 1) * t.spacing, (float)(t.samples_y - 1) * t.spacing);
            ImGui::Text("Samples: %u x %u", t.samples_x, t.samples_y);
            ImGui::Text("Tiles: %u x %u (%.2f m each)", t.tiles_x, t.tiles_y, (float)LTERRAIN_TILE_QUADS * t.spacing);
            if (t.source_bits) ImGui::Text("Source Depth: %u-bit", t.source_bits);
        } else {
            ImGui::TextDisabled("Empty. Generate a terrain or import a heightfield.");
        }
    }

    if (ui_category("Generate")) {
        const uint32_t count = (uint32_t)std::size(TerrainDocument::GENERATE_SIZES);
        if (tab->generate_size >= count) tab->generate_size = count - 1;
        const uint32_t samples = TerrainDocument::GENERATE_SIZES[tab->generate_size];
        char current[48];
        std::snprintf(current, sizeof(current), "%u x %u", samples, samples);
        if (ImGui::BeginCombo("Samples", current)) {
            for (uint32_t i = 0; i < count; i++) {
                char label[48];
                std::snprintf(label, sizeof(label), "%u x %u", TerrainDocument::GENERATE_SIZES[i], TerrainDocument::GENERATE_SIZES[i]);
                if (ImGui::Selectable(label, i == tab->generate_size)) tab->generate_size = i;
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Spacing##generate", &tab->generate_spacing, 0.001f, TerrainDocument::MIN_SPACING, TerrainDocument::MAX_SPACING, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::DragFloat("Height Min##generate", &tab->generate_height_min, 0.01f, -TerrainDocument::MAX_HEIGHT, tab->generate_height_max - TerrainDocument::MIN_HEIGHT_RANGE, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Lowest height the terrain can reach");
        ImGui::DragFloat("Height Max##generate", &tab->generate_height_max, 0.01f, tab->generate_height_min + TerrainDocument::MIN_HEIGHT_RANGE, TerrainDocument::MAX_HEIGHT, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Highest height the terrain can reach");
        const float meters = (float)(samples - 1) * tab->generate_spacing;
        ImGui::TextDisabled("Flat ground at 0 m, %.2f x %.2f m", meters, meters);
        if (ImGui::Button("Generate Flat")) tab->generate_flat(ctx);
        if (tab->has_data()) {
            ImGui::SameLine();
            if (ImGui::Button("Clear Terrain")) tab->clear_terrain(ctx);
        }
    }

    if (tab->has_data() && ui_category("Settings")) {
        ImGui::DragFloat("Spacing", &t.spacing, 0.001f, TerrainDocument::MIN_SPACING, TerrainDocument::MAX_SPACING, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Distance between height samples");
        ImGui::DragFloat("Height Min", &t.height_min, 0.01f, -TerrainDocument::MAX_HEIGHT, t.height_max - TerrainDocument::MIN_HEIGHT_RANGE, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Height of the lowest stored value");
        ImGui::DragFloat("Height Max", &t.height_max, 0.01f, t.height_min + TerrainDocument::MIN_HEIGHT_RANGE, TerrainDocument::MAX_HEIGHT, "%.3f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Height of the highest stored value");
    }

    ui_category_flush();
    if (tab->has_data() && t.source_bits == 8) ui_warning("Source is 8-bit, heights will show steps");
    ui_field_pop_style();
}

}
