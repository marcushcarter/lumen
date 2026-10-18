#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/assets/asset_field.h>
#include <drivers/imgui/imgui_helpers.h>
#include <imgui.h>

namespace lumen {

void ModelDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    if (imgui_category("Mesh")) asset_field(ctx, "###model_mesh", tab->model.mesh, AssetType::MESH, "Drop a mesh asset here");
}

}