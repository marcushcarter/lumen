#include <editor/documents/terrain/panels/terrain_details_panel.h>
#include <editor/documents/terrain/terrain_document.h>
#include <imgui.h>

namespace lumen {

void TerrainDetailsPanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    ImGui::TextDisabled("No terrain generated yet.");
}

}
