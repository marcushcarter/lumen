#include <editor/tabs/world/panels/world_settings_panel.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/editor_context.h>
#include <core/base/profiling.h>
#include <imgui.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <cfloat>
#include <editor/assets/import/mesh_cooker.h>

namespace lumen {
    
void WorldSettingsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.render_path) return;
}

}