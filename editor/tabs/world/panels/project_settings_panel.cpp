#include <editor/tabs/world/panels/project_settings_panel.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/import/icon_cooker.h>
#include <core/project/project.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace lumen {

void ProjectSettingsPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.project || !ctx.project->loaded()) return;
    Project& project = *ctx.project;
    ProjectSettings& s = project.settings;
    bool commit = false;

    ui_field_push_style();

    if (ImGui::GetActiveID() != ImGui::GetID("##title") && s.window_title != title_buf) std::snprintf(title_buf, sizeof(title_buf), "%s", s.window_title.c_str());
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

    ui_field_pop_style();

    if (icon_changed) {
        commit = true;
        if (IconCooker::cook_window_icon(project) != Error::OK) log_write("Project Settings: the window icon couldn't be cooked.");
    }
    if (commit && project.save() != Error::OK) log_write("Project Settings: failed to save '%s'.", project.root.string().c_str());
}

}
