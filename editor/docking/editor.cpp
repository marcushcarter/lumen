#include <editor/docking/editor.h>
#include <editor/docking/tab_strip.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <editor/docking/panels/world_settings_panel.h>
#include <editor/docking/panels/outliner_panel.h>

namespace lumen {

Error Editor::initialize()
{
    using enum Error;
    center_view.initialize();
    right_top.zone = DockZone::RIGHT_TOP;
    right_bottom.zone = DockZone::RIGHT_BOTTOM;

    add_panel<OutlinerPanel>();
    add_panel<WorldSettingsPanel>();

    return OK;
}

void Editor::shutdown()
{
    right_top.panels.clear();
    right_bottom.panels.clear();
    panels.clear();
}

void Editor::_draw_toolbar(EditorContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    {
        const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
        const bool paused = ctx.pie_is_paused && ctx.pie_is_paused();

        const float btn = ImGui::GetFrameHeight();
        const float spacing = 6.0f;
        const int count = playing ? 2 : 1;
        const float total = btn * count + spacing * (count - 1);

        ImGui::SameLine();
        const float center_x = (ImGui::GetContentRegionMax().x - total) * 0.5f;
        if (center_x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(center_x);

        if (!playing) {
            if (ImGui::Button(ICON_FA_PLAY, ImVec2(btn, btn)) && ctx.pie_toggle_play) ctx.pie_toggle_play();
        } else {
            if (ImGui::Button(ICON_FA_STOP, ImVec2(btn, btn)) && ctx.pie_toggle_play) ctx.pie_toggle_play();
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::Button(paused ? ICON_FA_PLAY : ICON_FA_PAUSE, ImVec2(btn, btn)) && ctx.pie_toggle_pause) ctx.pie_toggle_pause();
        }
    }

    const char* cog = ICON_FA_GEAR " Settings";
    float settings_width = ImGui::CalcTextSize(cog).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - settings_width);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));
    if (ImGui::Button(cog)) {
        // ctx.popups->open("Editor Settings");
        // settings_popup.open();
    }
    ImGui::PopStyleColor(3);
    
    ImGui::PopStyleVar();
}

void Editor::on_update(EditorContext& ctx, float)
{
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, ImFloor((bar_h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::BeginChild("##topbar", ImVec2(extent.x, bar_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    _draw_toolbar(ctx);
    ImGui::EndChild();

    const float body_y = origin.y + bar_h + DOCK_GAP;
    const float body_h = ImMax(1.0f, origin.y + extent.y - body_y);

    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragging_panel = payload && payload->IsDataType(DockWell::PAYLOAD);
    const bool top_live = right_top.has_open() || dragging_panel;
    const bool bottom_live = right_bottom.has_open() || dragging_panel;
    const bool right_live = top_live || bottom_live;
    const bool right_shown = right_live && !right_collapsed;

    const float min_dockwell = 0.025f;
    const float max_dockwell = 0.5f;
    const float body_w = ImMax(1.0f, extent.x - (right_live ? DOCK_GAP : 0.0f));
    float left_w = body_w;
    float right_w = 0.0f;
    if (right_shown) {
        const float lo = 1.0f - max_dockwell;
        float hi = 1.0f - min_dockwell;
        if (hi < lo) hi = lo;
        split_x = ImClamp(split_x, lo, hi);
        left_w = ImFloor(body_w * split_x);
        right_w = body_w - left_w;
    }

    center_view.draw(ctx, ImVec2(origin.x, body_y), ImVec2(origin.x + left_w, body_y + body_h));

    if (right_live) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + left_w, body_y));
        SplitterState sx = imgui_splitter("##split_lr", SplitAxis::X, ImVec2(DOCK_GAP, body_h), 0.0f);
        if (sx.active) {
            if (right_collapsed && sx.activated) split_x = 1.0f;
            split_x += sx.delta / body_w;
            right_collapsed = (1.0f - split_x < min_dockwell);
        }
    }

    if (right_shown) {
        const float rx = origin.x + left_w + DOCK_GAP;
        const float body_max_y = body_y + body_h;
        if (top_live && bottom_live) {
            const float col_h = ImMax(1.0f, body_h - DOCK_GAP);
            const float min_well = 0.15f;
            split_y = ImClamp(split_y, min_well, 1.0f - min_well);
            const float top_h = ImFloor(col_h * split_y);

            right_top.draw(ctx, ImVec2(rx, body_y), ImVec2(rx + right_w, body_y + top_h));

            ImGui::SetCursorScreenPos(ImVec2(rx, body_y + top_h));
            SplitterState sy = imgui_splitter("##split_tb", SplitAxis::Y, ImVec2(right_w, DOCK_GAP), 0.0f);
            if (sy.active) split_y += sy.delta / col_h;

            right_bottom.draw(ctx, ImVec2(rx, body_y + top_h + DOCK_GAP), ImVec2(rx + right_w, body_max_y));
        } else {
            DockWell& only = top_live ? right_top : right_bottom;
            only.draw(ctx, ImVec2(rx, body_y), ImVec2(rx + right_w, body_max_y));
        }
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
