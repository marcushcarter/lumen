#include <editor/docking/editor.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <editor/docking/panels/world_settings.h>

namespace lumen {

Error Editor::initialize()
{
    using enum Error;
    center_view.initialize();
    right_top.zone = DockZone::RIGHT_TOP;
    right_bottom.zone = DockZone::RIGHT_BOTTOM;

    // panels.push_back(std::make_unique<WorldPanel>());

    // panels.push_back(std::make_unique<DetailsPanel>());
    panels.push_back(std::make_unique<WorldSettingsPanel>());

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
        ImGuiWindowFlags_NoNavFocus
    );
    ImGui::PopStyleVar(3);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::BeginChild("##topbar", ImVec2(0, bar_h), true, ImGuiWindowFlags_NoScrollbar);
    _draw_toolbar(ctx);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const float thick = 6.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    float body_w = avail.x - thick;
    if (body_w < 1.0f) body_w = 1.0f;
    const float min_dockwell = 0.025f;
    const float max_dockwell = 0.5f;
    float left_w, right_w;
    if (right_collapsed) {
        left_w = body_w;
        right_w = 0.0f;
    } else {
        const float lo = 1.0f - max_dockwell;
        float hi = 1.0f - min_dockwell;
        if (hi < lo) hi = lo;
        split_x = ImClamp(split_x, lo, hi);
        left_w = ImFloor(body_w * split_x);
        right_w = body_w - left_w;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##left", ImVec2(left_w, avail.y), false, ImGuiWindowFlags_NoScrollbar);
    center_view.draw(ctx);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, 0);
    SplitterState sx = imgui_splitter("##split_lr", SplitAxis::X, ImVec2(thick, avail.y));
    if (sx.active) {
        if (right_collapsed && sx.activated) split_x = 1.0f;
        split_x += sx.delta / body_w;
        right_collapsed = (1.0f - split_x < min_dockwell);
    }

    if (!right_collapsed) {
        ImGui::SameLine(0, 0);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##right", ImVec2(right_w, avail.y), false, ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 ra = ImGui::GetContentRegionAvail();
            float col_h = ra.y - thick;
            if (col_h < 1.0f) col_h = 1.0f;
            const float min_well = 0.15f;
            split_y = ImClamp(split_y, min_well, 1.0f - min_well);
            float top_h = ImFloor(col_h * split_y);
            float bot_h = col_h - top_h;

            ImGui::BeginChild("##rtop", ImVec2(ra.x, top_h), true, ImGuiWindowFlags_NoScrollbar);
            right_top.draw(ctx);
            ImGui::EndChild();

            SplitterState sy = imgui_splitter("##split_tb", SplitAxis::Y, ImVec2(ra.x, thick));
            if (sy.active) split_y += sy.delta / col_h;

            ImGui::BeginChild("##rbottom", ImVec2(ra.x, bot_h), true, ImGuiWindowFlags_NoScrollbar);
            right_bottom.draw(ctx);
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::PopStyleVar();
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
