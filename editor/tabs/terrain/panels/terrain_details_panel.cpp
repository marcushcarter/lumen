#include <editor/tabs/terrain/panels/terrain_details_panel.h>
#include <editor/tabs/terrain/terrain_tab.h>
#include <imgui.h>

namespace lumen {

void TerrainDetailsPanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    ImGui::TextDisabled("No terrain generated yet.");
}

}
