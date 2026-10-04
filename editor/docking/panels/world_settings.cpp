#include <editor/docking/panels/world_settings.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/editor_context.h>
#include <core/base/profiling.h>
#include <imgui.h>

// #include <editor/popup/settings/editor_settings.h>
// #include <editor/popup/popup.h>
#include <editor/editor_settings.h>
#include <drivers/windows/window_driver_win32.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>

namespace lumen {
    
void WorldSettingsPanel::draw_contents(EditorContext& ctx)
{
    ImGui::Text("World Settings");
    
    ImGui::BeginDisabled(true);
    ImGui::DragInt("Window width", &ctx.project->settings.width);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Width")) ctx.project->settings.width = 1280;
    ImGui::DragInt("Window height", &ctx.project->settings.height);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Height")) ctx.project->settings.height = 720;
    ImGui::EndDisabled();

    if (!ctx.render_path) return;
    GeometryFeature& geo = ctx.render_path->geometry;
    const GeometryFeature::CullStats& s = geo.stats;
    // const uint32_t occluded = s.retest - s.phase2_visible;

    ImGui::Text("HiZ: %s", !geo.occlusion ? "off" : !geo.hiz_ok ? "unavailable" : geo.hiz_use_prev ? "active" : "warming up");
    if (!ctx.profiling || !ctx.profiling->cull_stats_on()) {
        ImGui::TextDisabled("Enable pipeline statistics in the GPU profiler to see counts.");
    } else {
        auto draw_occlusion = [](const char* p_title, const char* p_input_label, uint32_t p_input, uint32_t p_drawn_1, uint32_t p_deferred, uint32_t p_drawn_2) {
            const uint32_t occluded = p_deferred - p_drawn_2;
            ImGui::SeparatorText(p_title);
            ImGui::Text("%s: %u", p_input_label, p_input);
            ImGui::Text("Phase 1 drawn: %u", p_drawn_1);
            ImGui::Text("Phase 1 deferred: %u", p_deferred);
            ImGui::Text("Phase 2 drawn: %u", p_drawn_2);
            ImGui::Text("Occluded: %u (%.1f%%)", occluded, p_input ? 100.0f * (float)occluded / (float)p_input : 0.0f);
        };

        draw_occlusion("Instance Occlusion", "After frustum", s.instances_visible + s.instances_occluded, s.instances_visible, s.instances_occluded, s.instances_recovered);
        draw_occlusion("Cluster Occlusion", "After LOD + frustum", s.refs, s.phase1_visible, s.retest, s.phase2_visible);
    }

    ImGui::SeparatorText("Editor Settings");

    
    Theme& t = ctx.settings->theme;
    bool changed = false;

    if (ImGui::BeginCombo("Preset", Theme::theme_preset_name(t.preset))) {
        for (int i = 0; i < (int)std::size(Theme::THEME_PRESETS); ++i) {
            if (ImGui::Selectable(Theme::THEME_PRESETS[i].name, t.preset == i)) {
                t.preset  = i;
                t.base = Theme::THEME_PRESETS[i].base;
                t.accent = Theme::THEME_PRESETS[i].accent;
                t.text = Theme::THEME_PRESETS[i].text;
                changed = true;
            }
        }
        if (ImGui::Selectable("Custom", t.preset == -1)) { t.preset = -1; changed = true; }
        ImGui::EndCombo();
    }

    if (ImGui::ColorEdit3("Base", &t.base.x)) { t.preset = -1; changed = true; }
    if (ImGui::ColorEdit3("Text", &t.text.x)) { t.preset = -1; changed = true; }
    ImGui::BeginDisabled(t.use_system_accent);
    if (ImGui::ColorEdit3("Accent", &t.accent.x)) { t.preset = -1; changed = true; }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Use system accent", &t.use_system_accent)) changed = true;
    if (ImGui::Button("Reset to defaults")) {
        t = Theme{};
        changed = true;
    }

    if (changed) {
        t.apply();
        ImVec4 titlebar = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
        ctx.win32->window_set_titlebar_color(RGB((BYTE)(titlebar.x * 255), (BYTE)(titlebar.y * 255), (BYTE)(titlebar.z * 255)));
    }

    bool custom = ctx.win32->window.custom_titlebar;
    if (ImGui::Checkbox("Window Custom Titlebar", &custom)) ctx.win32->window_set_custom_titlebar(custom);


}

}