#include <editor/tools/settings/editor_settings_tool.h>
#include <editor/application/editor_settings.h>
#include <editor/ui/toolbar_row.h>
#include <editor/ui/ui_widgets.h>
#include <drivers/windows/window_driver_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <iterator>

namespace lumen {

Error EditorSettingsTool::create(EditorContext& ctx)
{
    (void)ctx;
    _title = "Editor Settings";
    layout.add("Appearance", [this](EditorContext& r_ctx) { _draw_appearance(r_ctx); });
    layout.add("Display", [this](EditorContext& r_ctx) { _draw_display(r_ctx); });
    layout.add("Startup", [this](EditorContext& r_ctx) { _draw_startup(r_ctx); });
    return Error::OK;
}

void EditorSettingsTool::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, [](EditorContext& r_ctx) {
        ToolbarRow bar;
        bar.begin();
        bar.button(ICON_FA_FLOPPY_DISK "  Save", "Editor settings save automatically", false, false);
        bar.end();
        (void)r_ctx;
    });
}

void EditorSettingsTool::_draw_appearance(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    static const char* THEMES[] = { "Dark", "Light" };
    int theme = s.theme.light ? 1 : 0;
    if (ImGui::Combo("Theme", &theme, THEMES, (int)std::size(THEMES))) {
        s.theme.light = theme == 1;
        s.theme_pending = true;
        s.dirty = true;
    }
}

void EditorSettingsTool::_draw_display(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    int v = (int)s.vsync_mode;
    if (ImGui::Combo("Vsync", &v, VSYNC_MODE_NAMES, (int)std::size(VSYNC_MODE_NAMES))) {
        s.vsync_mode = (VsyncMode)v;
        s.dirty = true;
    }
    if (ImGui::DragInt("FPS cap", &s.fps_cap, 1.0f, 0, 1000, s.fps_cap == 0 ? "Unlimited" : "%d", ImGuiSliderFlags_AlwaysClamp)) s.dirty = true;
}

void EditorSettingsTool::_draw_startup(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    if (ImGui::Checkbox("Open last project on startup", &s.reopen_last_project)) s.dirty = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Reopens the project you closed the editor in. Hold Shift while launching to skip it once.");
}

}
