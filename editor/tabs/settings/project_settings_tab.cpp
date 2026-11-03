#include <editor/tabs/settings/project_settings_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/import/icon_cooker.h>
#include <editor/assets/import/splash_cooker.h>
#include <editor/export/project_exporter.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/ui/ui_widgets.h>
#include <core/project/project.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <cstdio>

namespace lumen {

Error ProjectSettingsTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!ctx.project || !ctx.project->loaded()) return FAILED;
    saved = ctx.project->settings;
    edit = saved;
    _title = std::string(ICON) + "  Project Settings";
    layout.add("Window", [this](EditorContext& r_ctx) { _draw_window(r_ctx); });
    layout.add("Export", [this](EditorContext& r_ctx) { _draw_export(r_ctx); });
    return OK;
}

Error ProjectSettingsTab::save(EditorContext& ctx)
{
    using enum Error;
    Project& project = *ctx.project;
    const bool icon_changed = edit.window_icon != project.settings.window_icon;
    const bool splash_changed = edit.splash_image != project.settings.splash_image;
    project.settings = edit;
    if (project.save() != OK) {
        log_write("Project Settings: failed to save '%s'.", project.root.string().c_str());
        return FAILED;
    }
    saved = edit;
    if (icon_changed && IconCooker::cook_icon(project) != OK) log_write("Project Settings: the window icon couldn't be cooked.");
    if (splash_changed && SplashCooker::cook_splash(project) != OK) log_write("Project Settings: the splash image couldn't be cooked.");
    return OK;
}

void ProjectSettingsTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, "##ProjectSettingsHost", [this](EditorContext& r_ctx) {
        TabToolbar bar;
        bar.begin();
        bar.save(r_ctx, *this);
        bar.end();
    });
}

bool ProjectSettingsTab::_text_setting(const char* p_label, const char* p_hint, const char* p_tooltip, char* r_buf, std::string& r_value)
{
    if (ImGui::GetActiveID() != ImGui::GetID(p_label) && r_value != r_buf) std::snprintf(r_buf, MAX_TEXT, "%s", r_value.c_str());
    ImGui::InputTextWithHint(p_label, p_hint, r_buf, MAX_TEXT);
    const bool changed = ImGui::IsItemDeactivatedAfterEdit();
    if (changed) r_value = r_buf;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_tooltip);
    return changed;
}

void ProjectSettingsTab::_draw_window(EditorContext& ctx)
{
    const Project& project = *ctx.project;
    _text_setting("Title", project.name.c_str(), "Leave empty to use the project name", title_buf, edit.window_title);
    ImGui::DragInt("Width##v", &edit.width, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    if (ImGui::Button("Reset##w")) edit.width = ProjectSettings::DEFAULT_WIDTH;
    ImGui::DragInt("Height##v", &edit.height, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    if (ImGui::Button("Reset##h")) edit.height = ProjectSettings::DEFAULT_HEIGHT;
    asset_field(ctx, "Icon", "###window_icon", edit.window_icon, AssetType::TEXTURE);
    asset_field(ctx, "Splash Image", "###splash_image", edit.splash_image, AssetType::TEXTURE);
}

void ProjectSettingsTab::_draw_export(EditorContext& ctx)
{
    Project& project = *ctx.project;
    const std::string default_name = project.default_export_name();
    const std::string default_exe = default_name + ".exe";
    _text_setting("Folder##export", default_name.c_str(), "Name of the folder the export is written to", export_folder_buf, edit.export_folder);
    _text_setting("Executable##export", default_exe.c_str(), "Name of the exported game executable", export_exe_buf, edit.export_exe);
    ui_spacing();
    if (ImGui::Button(dirty() ? "Save and Export..." : "Export Project...", ImVec2(-FLT_MIN, 0.0f))) {
        if (!dirty() || save(ctx) == Error::OK) ProjectExporter::run_with_dialog(project);
    }
}

}
