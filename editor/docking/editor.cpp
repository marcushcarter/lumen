#include <editor/docking/editor.h>
#include <editor/docking/tab_strip.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <editor/docking/panels/outliner_panel.h>
#include <editor/docking/panels/details_panel.h>
#include <editor/docking/panels/world_settings_panel.h>

namespace lumen {

Error Editor::initialize()
{
    using enum Error;
    center_view.initialize();
    right_top.zone = DockZone::RIGHT_TOP;
    right_bottom.zone = DockZone::RIGHT_BOTTOM;

    add_panel<OutlinerPanel>();
    add_panel<DetailsPanel>();
    add_panel<WorldSettingsPanel>();

    return OK;
}

void Editor::shutdown()
{
    right_top.panels.clear();
    right_bottom.panels.clear();
    panels.clear();
}

static bool _toolbar_button(const char* p_label, ImVec2 p_min, ImVec2 p_size, bool p_active)
{
    ImGui::SetCursorScreenPos(p_min);
    const bool pressed = ImGui::InvisibleButton(p_label, p_size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(p_min.x + p_size.x, p_min.y + p_size.y - ui.stroke.hairline);
    if (p_active || held) dl->AddRectFilled(p_min, max, ui.color.panel);
    else if (hovered) dl->AddRectFilled(p_min, max, ui.color.hover);
    if (p_active) dl->AddRectFilled(ImVec2(p_min.x, max.y - ui.tab_strip.accent_height), max, ui.color.accent);

    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    dl->AddText(ImVec2(ImFloor(p_min.x + (p_size.x - ts.x) * 0.5f), ImFloor(p_min.y + (p_size.y - ts.y) * 0.5f)), (p_active || hovered) ? ui.color.text : ui.color.text_dim, p_label, end);
    return pressed;
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
    const char* end = ImGui::FindRenderedTextEnd(p_button.label);
    const ImVec2 ts = ImGui::CalcTextSize(p_button.label, end);
    dl->AddText(ImVec2(ImFloor(p_min.x + (p_size.x - ts.x) * 0.5f), ImFloor(p_min.y + (p_size.y - ts.y) * 0.5f)), col, p_button.label, end);
    return clicked && p_button.enabled;
}

void Editor::_draw_play_controls(EditorContext& ctx, ImVec2 p_bar_min, ImVec2 p_bar_max)
{
    const UiStyle& ui = ui_style();
    const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
    const bool paused = playing && ctx.pie_is_paused && ctx.pie_is_paused();

    const float box_h = (p_bar_max.y - p_bar_min.y) - ui.play.margin * 2.0f;
    const float bh = box_h - ui.play.inset * 2.0f;
    const float bw = bh + ui.play.button_extra_width;
    const float box_w = ui.play.inset * 2.0f + bw * 4.0f;
    const float total_w = box_w + ui.play.kebab_gap + ui.play.kebab_width;

    const ImVec2 box_min(ImFloor(p_bar_min.x + ((p_bar_max.x - p_bar_min.x) - total_w) * 0.5f), p_bar_min.y + ui.play.margin);
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

    const PlayButton step{ ICON_FA_FORWARD_STEP "##step", "Advance one frame", paused && (bool)ctx.pie_step_frame, ui.color.text };
    if (_play_button(step, ImVec2(x, y), size, false)) ctx.pie_step_frame();
    x += bw;

    const PlayButton stop{ ICON_FA_STOP "##stop", "Stop", playing && (bool)ctx.pie_toggle_play, ui.color.text };
    if (_play_button(stop, ImVec2(x, y), size, false)) ctx.pie_toggle_play();
    x += bw;

    const PlayButton eject{ ICON_FA_EJECT "##eject", "Eject", playing && (bool)ctx.pie_toggle_eject, ui.color.text };
    if (_play_button(eject, ImVec2(x, y), size, false)) ctx.pie_toggle_eject();

    const ImVec2 kebab_min(box_max.x + ui.play.kebab_gap, y);
    const bool options_open = ImGui::IsPopupOpen("##options_menu");
    const PlayButton options{ ICON_FA_ELLIPSIS_VERTICAL "##options", "Play options", true, ui.color.text };
    if (_play_button(options, kebab_min, ImVec2(ui.play.kebab_width, bh), options_open)) ImGui::OpenPopup("##options_menu");

    ImGui::SetNextWindowPos(ImVec2(kebab_min.x, box_max.y + ui.menu.popup_offset), ImGuiCond_Always);
    dock_menu_push_style();
    if (ImGui::BeginPopup("##options_menu", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::TextDisabled("MODES");
        ImGui::MenuItem(ICON_FA_WINDOW_MAXIMIZE "  New Window", nullptr, true);
        ImGui::EndPopup();
    }
    dock_menu_pop_style();

    ImGui::PopID();
}

void Editor::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    const ImVec2 wmax(wp.x + ws.x, wp.y + ws.y);
    const float h = ws.y;

    ImGui::PushClipRect(wp, wmax, false);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(wp, wmax, ui.color.chrome);
    dl->AddRectFilled(ImVec2(wp.x, wmax.y - ui.stroke.hairline), wmax, ui.color.line);

    _draw_play_controls(ctx, wp, ImVec2(wmax.x, wmax.y - ui.stroke.hairline));

    const char* cog = ICON_FA_GEAR "  Settings";
    const float cog_w = ImGui::CalcTextSize(cog).x + ui.tab_strip.pad_x * 2.0f;
    if (_toolbar_button(cog, ImVec2(wmax.x - cog_w, wp.y), ImVec2(cog_w, h), false)) {
        // ctx.popups->open("Editor Settings");
        // settings_popup.open();
    }

    ImGui::PopClipRect();
}

Editor::CornerState Editor::_corner_splitter(const char* p_id, ImVec2 p_cross, bool p_h_left, ImVec2 p_v_min, ImVec2 p_v_max, ImVec2 p_h_min, ImVec2 p_h_max)
{
    CornerState s;
    const UiStyle& ui = ui_style();
    const float gap = ui.dock.gap;
    ImGui::PushID(p_id);

    ImGui::SetCursorScreenPos(ImVec2(p_cross.x, p_cross.y - CORNER_REACH));
    ImGui::InvisibleButton("##v", ImVec2(gap, gap + CORNER_REACH * 2.0f));
    s.hovered |= ImGui::IsItemHovered();
    s.active |= ImGui::IsItemActive();
    s.activated |= ImGui::IsItemActivated();

    ImGui::SetCursorScreenPos(ImVec2(p_h_left ? p_cross.x - CORNER_REACH : p_cross.x + gap, p_cross.y));
    ImGui::InvisibleButton("##h", ImVec2(CORNER_REACH, gap));
    s.hovered |= ImGui::IsItemHovered();
    s.active |= ImGui::IsItemActive();
    s.activated |= ImGui::IsItemActivated();

    ImGui::PopID();

    if (!s.hovered && !s.active) return s;

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (s.active) s.delta = ImGui::GetIO().MouseDelta;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
    const float cx = ImFloor((p_v_min.x + p_v_max.x) * 0.5f);
    const float cy = ImFloor((p_h_min.y + p_h_max.y) * 0.5f);
    const float half = ui.splitter.line_width * 0.5f;
    dl->AddRectFilled(ImVec2(cx - half, p_v_min.y), ImVec2(cx + half, p_v_max.y), col);
    dl->AddRectFilled(ImVec2(p_h_min.x, cy - half), ImVec2(p_h_max.x, cy + half), col);
    return s;
}

void Editor::_apply_split_x(float p_delta, bool p_activated, float p_body_w)
{
    if (right_collapsed && p_activated) split_x = 1.0f;
    split_x += p_delta / p_body_w;
    right_collapsed = (1.0f - split_x < MIN_DOCKWELL);
}

void Editor::on_update(EditorContext& ctx, float)
{
    const UiStyle& ui = ui_style();
    const float gap = ui.dock.gap;
    const float bar_h = ui.toolbar.height;

    right_top.panels.clear();
    right_bottom.panels.clear();
    for (auto& p : panels) {
        switch (p->zone) {
            case DockZone::RIGHT_TOP: right_top.panels.push_back(p.get());    break;
            case DockZone::RIGHT_BOTTOM: right_bottom.panels.push_back(p.get()); break;
            case DockZone::LEFT: break;
        }
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##EditorHost", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar(3);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = ImGui::GetContentRegionAvail();

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.toolbar.padding_x, ImFloor((bar_h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::BeginChild("##topbar", ImVec2(extent.x, bar_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    _draw_toolbar(ctx);
    ImGui::EndChild();

    const float body_y = origin.y + bar_h + gap;
    const float body_h = ImMax(1.0f, origin.y + extent.y - body_y);

    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragging_panel = payload && payload->IsDataType(DockWell::PAYLOAD);
    const bool top_live = right_top.has_open() || dragging_panel;
    const bool bottom_live = right_bottom.has_open() || dragging_panel;
    const bool right_live = top_live || bottom_live;
    const bool right_shown = right_live && !right_collapsed;

    const float body_w = ImMax(1.0f, extent.x - (right_live ? gap : 0.0f));
    float left_w = body_w;
    float right_w = 0.0f;
    if (right_shown) {
        const float lo = 1.0f - MAX_DOCKWELL;
        float hi = 1.0f - MIN_DOCKWELL;
        if (hi < lo) hi = lo;
        split_x = ImClamp(split_x, lo, hi);
        left_w = ImFloor(body_w * split_x);
        right_w = body_w - left_w;
    }

    const float lx = origin.x + left_w;
    const float rx = lx + gap;
    const float body_max_y = body_y + body_h;
    const bool right_split = right_shown && top_live && bottom_live;
    const float col_h = ImMax(1.0f, body_h - gap);
    float top_h = 0.0f;
    if (right_split) {
        split_y = ImClamp(split_y, MIN_WELL, 1.0f - MIN_WELL);
        top_h = ImFloor(col_h * split_y);
    }

    CornerState corner_center;
    CornerState corner_right;
    if (right_live) {
        const float center_split_y = center_view.layout(ImVec2(origin.x, body_y), ImVec2(lx, body_max_y));
        const ImVec2 v_min(lx, body_y);
        const ImVec2 v_max(rx, body_max_y);
        corner_center = _corner_splitter("##corner_center", ImVec2(lx, center_split_y), true, v_min, v_max, ImVec2(origin.x, center_split_y), ImVec2(lx, center_split_y + gap));
        if (right_split) corner_right = _corner_splitter("##corner_right", ImVec2(lx, body_y + top_h), false, v_min, v_max, ImVec2(rx, body_y + top_h), ImVec2(rx + right_w, body_y + top_h + gap));
    }

    center_view.draw(ctx, ImVec2(origin.x, body_y), ImVec2(lx, body_max_y));

    if (right_live) {
        ImGui::SetCursorScreenPos(ImVec2(lx, body_y));
        SplitterState sx = imgui_splitter("##split_lr", SplitAxis::X, ImVec2(gap, body_h), 0.0f);
        if (sx.active) _apply_split_x(sx.delta, sx.activated, body_w);
    }

    if (right_shown) {
        if (right_split) {
            right_top.draw(ctx, ImVec2(rx, body_y), ImVec2(rx + right_w, body_y + top_h));
            ImGui::SetCursorScreenPos(ImVec2(rx, body_y + top_h));
            SplitterState sy = imgui_splitter("##split_tb", SplitAxis::Y, ImVec2(right_w, gap), 0.0f);
            if (sy.active) split_y += sy.delta / col_h;
            right_bottom.draw(ctx, ImVec2(rx, body_y + top_h + gap), ImVec2(rx + right_w, body_max_y));
        } else {
            DockWell& only = top_live ? right_top : right_bottom;
            only.draw(ctx, ImVec2(rx, body_y), ImVec2(rx + right_w, body_max_y));
        }
    }

    if (corner_center.active) {
        _apply_split_x(corner_center.delta.x, corner_center.activated, body_w);
        center_view.apply_split_delta(corner_center.delta.y, corner_center.activated);
    }
    if (corner_right.active) {
        _apply_split_x(corner_right.delta.x, corner_right.activated, body_w);
        split_y += corner_right.delta.y / col_h;
    }

    ImGui::End();
}

void Editor::draw_menu()
{
    if (ImGui::BeginMenu("Panels")) {
        for (auto& p : panels) ImGui::MenuItem(p->name(), nullptr, &p->open);
        ImGui::EndMenu();
    }
}

void Editor::take_screenshot(EditorContext& ctx)
{
    ctx.render_path->screenshot.requested = true;
}

}
