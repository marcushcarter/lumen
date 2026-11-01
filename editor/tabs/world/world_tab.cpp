#include <editor/tabs/world/world_tab.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/panels/shared_panels.h>
#include <editor/tabs/settings/editor_settings_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/panels/details_panel.h>
#include <editor/tabs/world/panels/outliner_panel.h>
#include <editor/tabs/world/panels/project_settings_panel.h>
#include <editor/tabs/world/panels/version_control_panel.h>
#include <editor/tabs/world/panels/world_settings_panel.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace lumen {

Error WorldTab::initialize(SharedPanels& r_shared)
{
    using enum Error;
    layout.initialize("##EditorHost");
    layout.add_panel<OutlinerPanel>();
    details = layout.add_panel<DetailsPanel>();
    layout.add_panel<WorldSettingsPanel>();
    layout.add_panel<VersionControlPanel>();
    layout.add_panel<ProjectSettingsPanel>();
    for (auto& p : r_shared.panels) layout.add_shared(p.get());
    return OK;
}

void WorldTab::shutdown()
{
    layout.shutdown();
}

struct PlayButton
{
    const char* label;
    const char* tooltip;
    bool enabled;
    ImU32 icon;
};

static bool _play_button(const PlayButton& p_button, ImVec2 p_min, ImVec2 p_size, bool p_open)
{
    ImGui::SetCursorScreenPos(p_min);
    const bool clicked = ImGui::InvisibleButton(p_button.label, p_size);
    const bool hovered = p_button.enabled && ImGui::IsItemHovered();
    const bool held = p_button.enabled && ImGui::IsItemActive();
    if (p_button.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_button.tooltip);

    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(p_min.x + p_size.x, p_min.y + p_size.y);
    if (held || p_open) dl->AddRectFilled(p_min, max, ui.color.line, ui.play.rounding);
    else if (hovered) dl->AddRectFilled(p_min, max, ui.color.panel, ui.play.rounding);

    const ImU32 col = p_button.enabled ? p_button.icon : ui_with_alpha(ui.color.text_dim, ui.alpha.disabled);
    ui_text_centered(dl, p_min, max, col, p_button.label, ImGui::FindRenderedTextEnd(p_button.label));
    return clicked && p_button.enabled;
}

float WorldTab::_draw_play_controls(EditorContext& ctx, ImVec2 p_bar_min, ImVec2 p_bar_max)
{
    const UiStyle& ui = ui_style();
    const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
    const bool paused = playing && ctx.pie_is_paused && ctx.pie_is_paused();

    const float box_h = (p_bar_max.y - p_bar_min.y) - ui.play.margin * 2.0f;
    const float bh = box_h - ui.play.inset * 2.0f;
    const float bw = bh + ui.play.button_extra_width;
    const float box_w = ui.play.inset * 2.0f + bw * 2.0f;
    const float total_w = box_w + ui.play.kebab_gap + ui.play.kebab_width;

    const ImVec2 box_min(p_bar_min.x, p_bar_min.y + ui.play.margin);
    const ImVec2 box_max(box_min.x + box_w, box_min.y + box_h);
    ImGui::GetWindowDrawList()->AddRectFilled(box_min, box_max, ui.color.sunken, ui.play.rounding + ui.play.inset);

    ImGui::PushID("##play_controls");
    const ImVec2 size(bw, bh);
    const float y = box_min.y + ui.play.inset;
    float x = box_min.x + ui.play.inset;

    PlayButton primary{ ICON_FA_PLAY "##primary", "Play", (bool)ctx.pie_toggle_play, ui.color.success };
    if (playing && paused) primary = { ICON_FA_PLAY "##primary", "Resume", (bool)ctx.pie_toggle_pause, ui.color.success };
    else if (playing) primary = { ICON_FA_PAUSE "##primary", "Pause", (bool)ctx.pie_toggle_pause, ui.color.text };
    if (_play_button(primary, ImVec2(x, y), size, false)) {
        if (!playing) ctx.pie_toggle_play();
        else ctx.pie_toggle_pause();
    }
    x += bw;

    const PlayButton stop{ ICON_FA_STOP "##stop", "Stop", playing && (bool)ctx.pie_toggle_play, ui.color.text };
    if (_play_button(stop, ImVec2(x, y), size, false)) ctx.pie_toggle_play();

    const ImVec2 kebab_min(box_max.x + ui.play.kebab_gap, y);
    const bool options_open = ImGui::IsPopupOpen("##options_menu");
    const PlayButton options{ ICON_FA_ELLIPSIS_VERTICAL "##options", "Play options", true, ui.color.text };
    if (_play_button(options, kebab_min, ImVec2(ui.play.kebab_width, bh), options_open)) ImGui::OpenPopup("##options_menu");

    ImGui::SetNextWindowPos(ImVec2(kebab_min.x, box_max.y + ui.menu.popup_offset), ImGuiCond_Always);
    ui_menu_push_style();
    if (ImGui::BeginPopup("##options_menu", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::TextDisabled("MODES");
        ImGui::MenuItem(ICON_FA_WINDOW_MAXIMIZE "  New Window", nullptr, true);
        ImGui::EndPopup();
    }
    ui_menu_pop_style();

    ImGui::PopID();
    return total_w;
}

void WorldTab::_draw_toolbar(EditorContext& ctx)
{
    TabToolbar bar;
    bar.begin();
    bar.save(ctx, *this);
    bar.gap();
    bar.x += _draw_play_controls(ctx, ImVec2(bar.x, bar.min.y), bar.inner_max());
    bar.gap();
    if (bar.button(ICON_FA_GEAR "  Settings")) ctx.tabs->open<EditorSettingsTab>();
    bar.end();
}

Error WorldTab::save(EditorContext& ctx)
{
    if (ctx.save_all_callback) ctx.save_all_callback();
    return Error::OK;
}

void WorldTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    if (ctx.selection && ctx.selection->version != selection_version) {
        selection_version = ctx.selection->version;
        if (!ctx.selection->empty() || ctx.selection->environment) layout.well(details->default_zone()).active_name = details->layout_id;
    }
    layout.draw(ctx, [this](EditorContext& r_ctx) { _draw_toolbar(r_ctx); });
}

void WorldTab::draw_menu()
{
    layout.draw_menu();
}

}
