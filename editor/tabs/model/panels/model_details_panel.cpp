#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/tabs/model/model_tab.h>
#include <imgui.h>

namespace lumen {

void ModelDetailsPanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    ImGui::Text("hello, World");
}

}