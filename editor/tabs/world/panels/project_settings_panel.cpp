#include <editor/tabs/world/panels/project_settings_panel.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/import/icon_cooker.h>
#include <editor/export/project_exporter.h>
#include <core/project/project.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <cstdio>

namespace lumen {

bool ProjectSettingsPanel::_text_setting(const char* p_label, const char* p_hint, const char* p_tooltip, char* r_buf, std::string& r_value)
{
    if (ImGui::GetActiveID() != ImGui::GetID(p_label) && r_value != r_buf) std::snprintf(r_buf, MAX_TITLE, "%s", r_value.c_str());
    ImGui::InputTextWithHint(p_label, p_hint, r_buf, MAX_TITLE);
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    if (commit) r_value = r_buf;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_tooltip);
    return commit;
}

void ProjectSettingsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.project || !ctx.project->loaded()) return;
    Project& project = *ctx.project;
    ProjectSettings& s = project.settings;
    bool commit = false;

    ui_field_push_style();

    if (ImGui::GetActiveID() != ImGui::GetID("Title") && s.window_title != title_buf) std::snprintf(title_buf, sizeof(title_buf), "%s", s.window_title.c_str());
    ImGui::InputTextWithHint("Title", project.name.c_str(), title_buf, sizeof(title_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.window_title = title_buf;
        commit = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Leave empty to use the project name");
    ImGui::DragInt("Width##v", &s.width, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset##w")) {
        s.width = ProjectSettings::DEFAULT_WIDTH;
        commit = true;
    }
    ImGui::DragInt("Height##v", &s.height, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset##h")) {
        s.height = ProjectSettings::DEFAULT_HEIGHT;
        commit = true;
    }
    const bool icon_changed = asset_thumbnail_field(ctx, "###window_icon", s.window_icon, AssetType::TEXTURE, "Drop a texture here");

    ImGui::SeparatorText("Export");
    const std::string default_name = project.default_export_name();
    const std::string default_exe = default_name + ".exe";
    if (_text_setting("Folder##export", default_name.c_str(), "Name of the folder the export is written to", export_folder_buf, s.export_folder)) commit = true;
    if (_text_setting("Executable##export", default_exe.c_str(), "Name of the exported game executable", export_exe_buf, s.export_exe)) commit = true;
    if (ImGui::Button("Export Project...", ImVec2(-FLT_MIN, 0.0f))) {
        if (commit && project.save() != Error::OK) log_write("Project Settings: failed to save '%s'.", project.root.string().c_str());
        commit = false;
        ProjectExporter::run_with_dialog(project);
    }

    ui_field_pop_style();

    if (icon_changed) {
        commit = true;
        if (IconCooker::cook_window_icon(project) != Error::OK) log_write("Project Settings: the window icon couldn't be cooked.");
    }
    if (commit && project.save() != Error::OK) log_write("Project Settings: failed to save '%s'.", project.root.string().c_str());
}

}
