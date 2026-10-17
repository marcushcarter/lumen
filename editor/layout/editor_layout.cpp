#include <editor/layout/editor_layout.h>
#include <editor/ui/tab_strip.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace lumen {

void EditorLayout::initialize(const char* p_host_id)
{
    host_id = p_host_id;
    right_top.zone = DockZone::RIGHT_TOP;
    right_bottom.zone = DockZone::RIGHT_BOTTOM;
}

void EditorLayout::shutdown()
{
    right_top.panels.clear();
    right_bottom.panels.clear();
    panels.clear();
}

EditorLayout::CornerState EditorLayout::_corner_splitter(const char* p_id, ImVec2 p_cross, bool p_h_left, ImVec2 p_v_min, ImVec2 p_v_max, ImVec2 p_h_min, ImVec2 p_h_max)
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

void EditorLayout::_apply_split_x(float p_delta, bool p_activated, float p_body_w)
{
    if (right_collapsed && p_activated) split_x = 1.0f;
    split_x += p_delta / p_body_w;
    right_collapsed = (1.0f - split_x < MIN_DOCKWELL);
}

float EditorLayout::_layout_center(ImVec2 p_min, ImVec2 p_max)
{
    const float h = p_max.y - p_min.y;
    _usable_h = ImMax(1.0f, h - TabStrip::height() - ui_style().dock.gap);
    if (drawer.collapsed) {
        _scene_h = _usable_h;
        _drawer_h = 0.0f;
    } else {
        float min_r = 1.0f - MAX_DRAWER;
        float max_r = 1.0f - MIN_DRAWER;
        if (max_r < min_r) max_r = min_r;
        center_split = ImClamp(center_split, min_r, max_r);
        _scene_h = ImFloor(_usable_h * center_split);
        _drawer_h = _usable_h - _scene_h;
    }
    return p_min.y + _scene_h;
}

void EditorLayout::_apply_center_delta(float p_delta, bool p_activated)
{
    if (drawer.collapsed && p_activated) center_split = 1.0f;
    center_split += p_delta / _usable_h;
    drawer.collapsed = (1.0f - center_split < MIN_DRAWER);
}

void EditorLayout::_draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const float gap = ui.dock.gap;
    const float w = p_max.x - p_min.x;
    const float strip_h = TabStrip::height();
    _layout_center(p_min, p_max);

    viewport.draw(ctx, p_min, ImVec2(p_max.x, p_min.y + ImMax(1.0f, _scene_h)));

    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + _scene_h));
    SplitterState s = imgui_splitter("##vsplit", SplitAxis::Y, ImVec2(w, gap), 0.0f);
    if (s.active) _apply_center_delta(s.delta, s.activated);

    if (!ctx.drawer) return;

    if (!drawer.collapsed && _drawer_h >= 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + _scene_h + gap));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.dock.drawer_padding);
        ImGui::BeginChild("##drawer", ImVec2(w, _drawer_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ctx.drawer->draw_content(ctx, drawer);
        ImGui::EndChild();
    }

    const bool was_collapsed = drawer.collapsed;
    ctx.drawer->draw_strip(ImVec2(p_min.x, p_max.y - strip_h), p_max, drawer);
    if (was_collapsed && !drawer.collapsed) center_split = 1.0f - MAX_DRAWER / 3.0f;
}

void EditorLayout::draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar)
{
    const UiStyle& ui = ui_style();
    const float gap = ui.dock.gap;
    const float bar_h = ui.toolbar.height;

    right_top.panels.clear();
    right_bottom.panels.clear();
    for (auto& p : panels) {
        switch (p->zone) {
            case DockZone::RIGHT_TOP: right_top.panels.push_back(p.get()); break;
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
    ImGui::Begin(host_id.c_str(), nullptr,
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
    if (p_toolbar) p_toolbar(ctx);
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
        const float center_split_y = _layout_center(ImVec2(origin.x, body_y), ImVec2(lx, body_max_y));
        const ImVec2 v_min(lx, body_y);
        const ImVec2 v_max(rx, body_max_y);
        corner_center = _corner_splitter("##corner_center", ImVec2(lx, center_split_y), true, v_min, v_max, ImVec2(origin.x, center_split_y), ImVec2(lx, center_split_y + gap));
        if (right_split) corner_right = _corner_splitter("##corner_right", ImVec2(lx, body_y + top_h), false, v_min, v_max, ImVec2(rx, body_y + top_h), ImVec2(rx + right_w, body_y + top_h + gap));
    }

    _draw_center(ctx, ImVec2(origin.x, body_y), ImVec2(lx, body_max_y));

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
        _apply_center_delta(corner_center.delta.y, corner_center.activated);
    }
    if (corner_right.active) {
        _apply_split_x(corner_right.delta.x, corner_right.activated, body_w);
        split_y += corner_right.delta.y / col_h;
    }

    ImGui::End();
}

void EditorLayout::draw_menu()
{
    if (ImGui::BeginMenu("Panels")) {
        for (auto& p : panels) ImGui::MenuItem(p->name(), nullptr, &p->open);
        ImGui::EndMenu();
    }
}

}