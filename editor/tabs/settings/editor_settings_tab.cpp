#include <editor/tabs/settings/editor_settings_tab.h>
#include <editor/application/editor_settings.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/ui/ui_widgets.h>
#include <drivers/windows/window_driver_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <iterator>

namespace lumen {

Error EditorSettingsTab::create(EditorContext& ctx)
{
    (void)ctx;
    _title = std::string(ICON) + "  Editor Settings";
    layout.add("Appearance", [this](EditorContext& r_ctx) { _draw_appearance(r_ctx); });
    layout.add("Display", [this](EditorContext& r_ctx) { _draw_display(r_ctx); });
    layout.add("Startup", [this](EditorContext& r_ctx) { _draw_startup(r_ctx); });
    return Error::OK;
}

void EditorSettingsTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, "##EditorSettingsHost", [](EditorContext& r_ctx) {
        TabToolbar bar;
        bar.begin();
        bar.button(ICON_FA_FLOPPY_DISK "  Save", "Editor settings save automatically", false, false);
        bar.end();
        (void)r_ctx;
    });
}

void EditorSettingsTab::_draw_appearance(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    bool custom = ctx.win32->window_has_custom_titlebar(*ctx.main_window);
    if (ImGui::Checkbox("Custom titlebar", &custom)) {
        ctx.win32->window_set_custom_titlebar(*ctx.main_window, custom);
        s.dirty = true;
    }
}

void EditorSettingsTab::_draw_display(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    int v = (int)s.vsync_mode;
    if (ImGui::Combo("Vsync", &v, VSYNC_MODE_NAMES, (int)std::size(VSYNC_MODE_NAMES))) {
        s.vsync_mode = (VsyncMode)v;
        s.dirty = true;
    }
    if (ImGui::DragInt("FPS cap", &s.fps_cap, 1.0f, 0, 1000, s.fps_cap == 0 ? "Unlimited" : "%d", ImGuiSliderFlags_AlwaysClamp)) s.dirty = true;
}

void EditorSettingsTab::_draw_startup(EditorContext& ctx)
{
    EditorSettings& s = *ctx.settings;
    if (ImGui::Checkbox("Open last project on startup", &s.reopen_last_project)) s.dirty = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Reopens the project you closed the editor in. Hold Shift while launching to skip it once.");
}

}
