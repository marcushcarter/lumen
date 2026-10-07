#include <editor/docking/panels/world_settings_panel.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/editor_context.h>
#include <core/base/profiling.h>
#include <imgui.h>
#include <drivers/imgui/imgui_helpers.h>
#include <cfloat>

// #include <editor/popup/settings/editor_settings.h>
// #include <editor/popup/popup.h>
#include <editor/editor_settings.h>
#include <drivers/windows/window_driver_win32.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>

namespace lumen {
    
void WorldSettingsPanel::draw_contents(EditorContext& ctx)
{
    auto reset_row = [](const char* p_label, int* p_value, int p_default) {
        imgui_property(p_label);
        const float bw = ImGui::GetFrameHeight();
        const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::PushID(p_label);
        ImGui::SetNextItemWidth(-(bw + sp));
        ImGui::DragInt("##v", p_value);
        ImGui::SameLine(0.0f, sp);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(bw, bw))) *p_value = p_default;
        ImGui::PopID();
    };

    ImGui::BeginDisabled(true);
    if (imgui_property_grid_begin("##window")) {
        reset_row("Window width", &ctx.project->settings.width, 1280);
        reset_row("Window height", &ctx.project->settings.height, 720);
        imgui_property_grid_end();
    }
    ImGui::EndDisabled();

    if (!ctx.render_path) return;
    GeometryFeature& geo = ctx.render_path->geometry;
    const GeometryFeature::CullStats& s = geo.stats;

    ImGui::SeparatorText("Culling");
    if (imgui_property_grid_begin("##hiz")) {
        imgui_property("HiZ");
        ImGui::TextUnformatted(!geo.occlusion ? "off" : !geo.hiz_ok ? "unavailable" : geo.hiz_use_prev ? "active" : "warming up");
        imgui_property_grid_end();
    }
    if (!ctx.profiling || !ctx.profiling->cull_stats_on()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Enable pipeline statistics in the GPU profiler to see counts.");
        ImGui::PopTextWrapPos();
    } else {
        auto draw_occlusion = [](const char* p_title, const char* p_input_label, uint32_t p_input, uint32_t p_drawn_1, uint32_t p_deferred, uint32_t p_drawn_2) {
            const uint32_t occluded = p_deferred - p_drawn_2;
            ImGui::SeparatorText(p_title);
            if (!imgui_property_grid_begin(p_title, 0.55f)) return;
            imgui_property(p_input_label); ImGui::Text("%u", p_input);
            imgui_property("Phase 1 drawn"); ImGui::Text("%u", p_drawn_1);
            imgui_property("Phase 1 deferred"); ImGui::Text("%u", p_deferred);
            imgui_property("Phase 2 drawn"); ImGui::Text("%u", p_drawn_2);
            imgui_property("Occluded"); ImGui::Text("%u (%.1f%%)", occluded, p_input ? 100.0f * (float)occluded / (float)p_input : 0.0f);
            imgui_property_grid_end();
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

    bool custom = ctx.win32->window.custom_titlebar;
    if (ImGui::Checkbox("Custom titlebar", &custom)) ctx.win32->window_set_custom_titlebar(custom);

    if (ImGui::Button("Reset to defaults", ImVec2(-FLT_MIN, 0.0f))) {
        t = Theme{};
        changed = true;
    }

    if (changed) {
        t.apply();
        ctx.win32->window_set_titlebar_color((COLORREF)Theme::to_colorref(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]));
    }
}

}