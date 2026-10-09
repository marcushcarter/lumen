#include <editor/settings/settings_page.h>
#include <editor/editor_settings.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/windows/window_driver_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <cfloat>
#include <iterator>

namespace lumen {

static void _theme_apply(EditorContext& ctx)
{
    ctx.settings->theme.apply();
    ctx.win32->window_set_titlebar_color((COLORREF)Theme::to_colorref(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]));
}

void SettingsPage::draw(EditorContext& ctx)
{
    static const EditorSettings DEFAULT_SETTINGS{};
    EditorSettings& s = *ctx.settings;
    bool theme_changed = false;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##SettingsHost", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar(3);

    if (ImGui::BeginCombo("Preset", Theme::theme_preset_name(s.theme.preset))) {
        for (int i = 0; i < (int)std::size(Theme::THEME_PRESETS); ++i) {
            if (ImGui::Selectable(Theme::THEME_PRESETS[i].name, s.theme.preset == i)) {
                s.theme.preset  = i;
                s.theme.base = Theme::THEME_PRESETS[i].base;
                s.theme.accent = Theme::THEME_PRESETS[i].accent;
                s.theme.text = Theme::THEME_PRESETS[i].text;
                theme_changed = true;
            }
        }
        if (ImGui::Selectable("Custom", s.theme.preset == -1)) { s.theme.preset = -1; theme_changed = true; }
        ImGui::EndCombo();
    }

    if (ImGui::ColorEdit3("Base", &s.theme.base.x)) { s.theme.preset = -1; theme_changed = true; }
    if (ImGui::ColorEdit3("Text", &s.theme.text.x)) { s.theme.preset = -1; theme_changed = true; }
    ImGui::BeginDisabled(s.theme.use_system_accent);
    if (ImGui::ColorEdit3("Accent", &s.theme.accent.x)) { s.theme.preset = -1; theme_changed = true; }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Use system accent", &s.theme.use_system_accent)) theme_changed = true;

    bool custom = ctx.win32->window.custom_titlebar;
    if (ImGui::Checkbox("Custom titlebar", &custom)) ctx.win32->window_set_custom_titlebar(custom);

    if (ImGui::Button("Reset to default theme", ImVec2(-FLT_MIN, 0.0f))) {
        s.theme = Theme{};
        theme_changed = true;
    }

    int v = (int)s.vsync_mode;
    if (ImGui::Combo("Vsync", &v, VSYNC_MODE_NAMES, 3)) { s.vsync_mode = (VsyncMode)v; s.dirty = true; }
    if (ImGui::DragInt("Fps_cap", &s.fps_cap, 1.0f, 0, 1000, s.fps_cap == 0 ? "Unlimited" : "%d", ImGuiSliderFlags_AlwaysClamp)) s.dirty = true;
        
    if (theme_changed) _theme_apply(ctx);

    ImGui::End();
}

}