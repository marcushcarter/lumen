#include <editor/layout/editor_layout.h>
#include <editor/panels/output_panel.h>
#include <drivers/imgui/tab_strip.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

namespace lumen {

void EditorLayout::initialize(const char* p_host_id)
{
    host_id = p_host_id;
    bottom.zone = DockZone::BOTTOM;
    left.top.zone = DockZone::LEFT_TOP;
    left.bottom.zone = DockZone::LEFT_BOTTOM;
    right.top.zone = DockZone::RIGHT_TOP;
    right.bottom.zone = DockZone::RIGHT_BOTTOM;
}

void EditorLayout::shutdown()
{
    for (DockWell* w : { &bottom, &left.top, &left.bottom, &right.top, &right.bottom }) w->slots.clear();
    for (auto& p : owned) p->shutdown();
    slots.clear();
    owned.clear();
}

void EditorLayout::add_shared(Panel* p_panel)
{
    p_panel->_resolve_names();
    slots.push_back({ p_panel, p_panel->default_zone(), p_panel->default_open() });
}

DockSlot* EditorLayout::find(const char* p_name)
{
    // for (DockSlot& s : slots) if (std::strcmp(s.panel->name(), p_name) == 0) return &s;
    for (DockSlot& s : slots) if (std::strcmp(s.panel->layout_id, p_name) == 0 || std::strcmp(s.panel->title, p_name) == 0) return &s;
    return nullptr;
}

DockSlot* EditorLayout::find(const Panel* p_panel)
{
    for (DockSlot& s : slots) if (s.panel == p_panel) return &s;
    return nullptr;
}

DockWell& EditorLayout::well(DockZone p_zone)
{
    switch (p_zone) {
        case DockZone::BOTTOM: return bottom;
        case DockZone::LEFT_TOP: return left.top;
        case DockZone::LEFT_BOTTOM: return left.bottom;
        case DockZone::RIGHT_TOP: return right.top;
        case DockZone::RIGHT_BOTTOM: return right.bottom;
    }
    return right.bottom;
}

void EditorLayout::_uncollapse(DockZone p_zone)
{
    if ((p_zone == DockZone::LEFT_TOP || p_zone == DockZone::LEFT_BOTTOM) && left.collapsed) {
        left.collapsed = false;
        split_left = DEFAULT_SPLIT_LEFT;
    }
    if ((p_zone == DockZone::RIGHT_TOP || p_zone == DockZone::RIGHT_BOTTOM) && right.collapsed) {
        right.collapsed = false;
        split_right = DEFAULT_SPLIT_RIGHT;
    }
}

void EditorLayout::open_panel(DockSlot& r_slot)
{
    r_slot.open = true;
    well(r_slot.zone).active_name = r_slot.panel->layout_id;
    _uncollapse(r_slot.zone);
}

void EditorLayout::_apply_drop()
{
    if (!pending_drop.panel) return;
    if (DockSlot* s = find(pending_drop.panel)) {
        s->zone = pending_drop.zone;
        open_panel(*s);
    }
    pending_drop = {};
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

void EditorLayout::_apply_split_left(float p_delta, bool p_activated, float p_body_w)
{
    if (left.collapsed && p_activated) split_left = 0.0f;
    split_left += p_delta / p_body_w;
    left.collapsed = split_left < MIN_DOCKWELL;
}

void EditorLayout::_apply_split_right(float p_delta, bool p_activated, float p_body_w)
{
    if (right.collapsed && p_activated) split_right = 1.0f;
    split_right += p_delta / p_body_w;
    right.collapsed = (1.0f - split_right < MIN_DOCKWELL);
}

float EditorLayout::_layout_center(float p_top, float p_bottom)
{
    const float h = ImMax(1.0f, p_bottom - p_top);
    if (!bottom.has_open()) {
        usable_h = h;
        scene_h = h;
        bottom_h = 0.0f;
        bottom_was_open = false;
        return p_top + scene_h;
    }
    usable_h = ImMax(1.0f, h - ui_style().dock.gap);
    if (!bottom_was_open) center_split = OPEN_SPLIT;
    bottom_was_open = true;
    const float min_r = 1.0f - MAX_DRAWER;
    float max_r = drawer_dragging ? 1.0f : 1.0f - MIN_DRAWER;
    if (max_r < min_r) max_r = min_r;
    center_split = ImClamp(center_split, min_r, max_r);
    const bool snapped = drawer_dragging && 1.0f - center_split < MIN_DRAWER;
    scene_h = snapped ? usable_h : ImFloor(usable_h * center_split);
    bottom_h = usable_h - scene_h;
    return p_top + scene_h;
}

void EditorLayout::_apply_center_delta(float p_delta, bool p_activated)
{
    if (p_activated) {
        drawer_dragging = true;
        drawer_drag_start = center_split;
    }
    center_split += p_delta / usable_h;
}

void EditorLayout::_end_drawer_drag()
{
    drawer_dragging = false;
    if (1.0f - center_split >= MIN_DRAWER) return;
    for (DockSlot* s : bottom.slots) s->open = false;
    center_split = drawer_drag_start;
}

void EditorLayout::_draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const float gap = ui_style().dock.gap;
    const float w = p_max.x - p_min.x;

    viewport.draw(ctx, p_min, ImVec2(p_max.x, p_min.y + ImMax(1.0f, scene_h)));

    if (!bottom.has_open()) return;
    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + scene_h));
    SplitterState s = imgui_splitter("##vsplit", SplitAxis::Y, ImVec2(w, gap), 0.0f);
    if (s.active) _apply_center_delta(s.delta, s.activated);

    const float by = p_min.y + scene_h + gap;
    if (bottom_h >= 1.0f) bottom.draw(ctx, ImVec2(p_min.x, by), ImVec2(p_max.x, by + bottom_h));
}

void EditorLayout::_column_layout(Column& r_column, bool p_dragging, float p_col_h, bool& r_top, bool& r_bottom, float& r_top_h)
{
    r_top = r_column.top.has_open() || p_dragging;
    r_bottom = r_column.bottom.has_open() || p_dragging;
    if (!r_top && !r_bottom) r_top = true;
    r_top_h = 0.0f;
    if (r_top && r_bottom) {
        r_column.split_y = ImClamp(r_column.split_y, MIN_WELL, 1.0f - MIN_WELL);
        r_top_h = ImFloor(p_col_h * r_column.split_y);
    }
}

void EditorLayout::_draw_column(EditorContext& ctx, Column& r_column, bool p_dragging, ImVec2 p_min, ImVec2 p_max)
{
    const float gap = ui_style().dock.gap;
    const float w = p_max.x - p_min.x;
    const float col_h = ImMax(1.0f, (p_max.y - p_min.y) - gap);
    bool show_top = false;
    bool show_bottom = false;
    float top_h = 0.0f;
    _column_layout(r_column, p_dragging, col_h, show_top, show_bottom, top_h);

    if (!(show_top && show_bottom)) {
        (show_top ? r_column.top : r_column.bottom).draw(ctx, p_min, p_max);
        return;
    }
    r_column.top.draw(ctx, p_min, ImVec2(p_max.x, p_min.y + top_h));
    ImGui::PushID(to_string(r_column.top.zone));
    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + top_h));
    SplitterState sy = imgui_splitter("##split_tb", SplitAxis::Y, ImVec2(w, gap), 0.0f);
    if (sy.active) r_column.split_y += sy.delta / col_h;
    ImGui::PopID();
    r_column.bottom.draw(ctx, ImVec2(p_min.x, p_min.y + top_h + gap), p_max);
}

void EditorLayout::_edge_target(const char* p_id, ImVec2 p_min, DockZone p_zone)
{
    const UiStyle& ui = ui_style();
    const float size = ui.dock.edge_target_size;
    const float inset = ui.dock.edge_target_inset;
    const float rounding = ui.dock.edge_target_rounding;
    const ImVec2 p_max(p_min.x + size, p_min.y + size);

    bool hovered = false;
    if (ImGui::BeginDragDropTargetCustom(ImRect(p_min, p_max), ImGui::GetID(p_id))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(DockWell::PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            hovered = pl->IsPreview();
            if (pl->IsDelivery()) pending_drop = { *reinterpret_cast<Panel* const*>(pl->Data), p_zone };
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
    fg->AddRectFilled(p_min, p_max, ui.color.chrome, rounding);
    if (hovered) fg->AddRectFilled(p_min, p_max, ui.color.drop_fill, rounding);
    fg->AddRect(p_min, p_max, hovered ? ui.color.drop_line : ui.color.line, rounding, 0, ui.dock.drop_outline);

    const ImVec2 a(p_min.x + inset, p_min.y + inset);
    const ImVec2 b(p_max.x - inset, p_max.y - inset);
    const float iw = b.x - a.x;
    const float ih = b.y - a.y;
    const float half_gap = ui.dock.gap * 0.5f;
    ImVec2 ia = a;
    ImVec2 ib = b;
    switch (p_zone) {
        case DockZone::LEFT_TOP: ib = ImVec2(a.x + iw * 0.4f, a.y + ih * 0.5f - half_gap); break;
        case DockZone::LEFT_BOTTOM: ia = ImVec2(a.x, a.y + ih * 0.5f + half_gap); ib = ImVec2(a.x + iw * 0.4f, b.y); break;
        case DockZone::RIGHT_TOP: ia = ImVec2(b.x - iw * 0.4f, a.y); ib = ImVec2(b.x, a.y + ih * 0.5f - half_gap); break;
        case DockZone::RIGHT_BOTTOM: ia = ImVec2(b.x - iw * 0.4f, a.y + ih * 0.5f + half_gap); break;
        case DockZone::BOTTOM: ia = ImVec2(a.x, b.y - ih * 0.4f); break;
    }
    fg->AddRect(a, b, ui.color.line, rounding * 0.5f);
    fg->AddRectFilled(ImVec2(ImFloor(ia.x), ImFloor(ia.y)), ImVec2(ImFloor(ib.x), ImFloor(ib.y)), hovered ? ui.color.drop_line : ui.color.text_dim, rounding * 0.5f);
}

void EditorLayout::_draw_footer(ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const char* console_label = ICON_FA_TERMINAL "  Cmd";
    float natural = 0.0f;
    for (const DockSlot& s : slots) if (s.panel->in_footer()) natural += TabStrip::natural_width(s.panel->title);

    const float console_chrome = ImGui::CalcTextSize(console_label).x + ui.console_bar.pad * 3.0f;
    const float free_w = (p_max.x - p_min.x) - natural - console_chrome;
    const float field_w = ImClamp(free_w, ui.console_bar.field_min_width, ui.console_bar.field_width);

    TabStrip strip;
    strip.begin("##footer", p_min, p_max, TabStrip::Edge::BOTTOM, natural, console_chrome + field_w);
    for (DockSlot& s : slots) {
        if (!s.panel->in_footer() || !strip.button(s.panel->title, s.open)) continue;
        if (s.open) s.open = false;
        else open_panel(s);
    }
    if (strip.console_field("##console_cmd", console_label, "Enter Console Command", console_buffer, sizeof(console_buffer), field_w)) {
        console_submit(console_buffer);
        console_buffer[0] = '\0';
    }
    strip.end();
}

void EditorLayout::draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar)
{
    const UiStyle& ui = ui_style();
    const float gap = ui.dock.gap;
    const float bar_h = p_toolbar ? ui.toolbar.height : 0.0f;

    for (DockWell* w : { &bottom, &left.top, &left.bottom, &right.top, &right.bottom }) {
        w->slots.clear();
        w->dropped = nullptr;
    }
    for (DockSlot& s : slots) {
        if (!s.panel->pinned()) continue;
        s.open = true;
        s.zone = s.panel->default_zone();
        well(s.zone).slots.push_back(&s);
    }
    for (DockSlot& s : slots) if (!s.panel->pinned()) well(s.zone).slots.push_back(&s);

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
    const ImVec2 end(origin.x + extent.x, origin.y + extent.y);

    if (p_toolbar) {
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.toolbar.padding_x, ImFloor((bar_h - ImGui::GetFrameHeight()) * 0.5f)));
        ImGui::BeginChild("##topbar", ImVec2(extent.x, bar_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        p_toolbar(ctx);
        ImGui::EndChild();
    }

    const float body_y = p_toolbar ? origin.y + bar_h + gap : origin.y;

    if (viewport.fullscreen) {
        viewport.draw(ctx, ImVec2(origin.x, body_y), end);
        ImGui::End();
        return;
    }

    const float footer_y = end.y - TabStrip::height();
    const float body_max_y = ImMax(body_y + 1.0f, footer_y - gap);
    const float body_h = body_max_y - body_y;
    const float col_h = ImMax(1.0f, body_h - gap);

    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragging = payload && payload->IsDataType(DockWell::PAYLOAD);

    const bool left_live = left.top.has_open() || left.bottom.has_open();
    const bool right_live = right.top.has_open() || right.bottom.has_open();
    const bool left_shown = left_live && !left.collapsed;
    const bool right_shown = right_live && !right.collapsed;

    const float body_w = ImMax(1.0f, extent.x - (left_live ? gap : 0.0f) - (right_live ? gap : 0.0f));
    float lf = 0.0f;
    float rf = 0.0f;
    if (right_shown) {
        rf = ImClamp(1.0f - split_right, MIN_DOCKWELL, MAX_DOCKWELL);
        split_right = 1.0f - rf;
    }
    if (left_shown) {
        const float hi = ImMax(MIN_DOCKWELL, ImMin(MAX_DOCKWELL, 1.0f - MIN_CENTER - rf));
        lf = ImClamp(split_left, MIN_DOCKWELL, hi);
        split_left = lf;
    }
    const float left_w = ImFloor(body_w * lf);
    const float right_w = ImFloor(body_w * rf);
    const float center_w = body_w - left_w - right_w;

    const float lx0 = origin.x;
    const float lx1 = lx0 + left_w;
    const float cx0 = lx1 + (left_live ? gap : 0.0f);
    const float cx1 = cx0 + center_w;
    const float rx0 = cx1 + (right_live ? gap : 0.0f);
    const float rx1 = end.x;

    const float center_split_y = _layout_center(body_y, body_max_y);
    const bool bottom_open = bottom.has_open();

    bool lt = false, lb = false, rt = false, rb = false;
    float lth = 0.0f, rth = 0.0f;
    if (left_shown) _column_layout(left, dragging, col_h, lt, lb, lth);
    if (right_shown) _column_layout(right, dragging, col_h, rt, rb, rth);

    const ImVec2 left_v_min(lx1, body_y);
    const ImVec2 left_v_max(cx0, body_max_y);
    const ImVec2 right_v_min(cx1, body_y);
    const ImVec2 right_v_max(rx0, body_max_y);
    CornerState corner_left;
    CornerState corner_right;
    CornerState corner_center_left;
    CornerState corner_center_right;
    if (left_shown && lt && lb) corner_left = _corner_splitter("##corner_left", ImVec2(lx1, body_y + lth), true, left_v_min, left_v_max, ImVec2(lx0, body_y + lth), ImVec2(lx1, body_y + lth + gap));
    if (right_shown && rt && rb) corner_right = _corner_splitter("##corner_right", ImVec2(cx1, body_y + rth), false, right_v_min, right_v_max, ImVec2(rx0, body_y + rth), ImVec2(rx1, body_y + rth + gap));
    if (bottom_open && left_live) corner_center_left = _corner_splitter("##corner_center_left", ImVec2(lx1, center_split_y), false, left_v_min, left_v_max, ImVec2(cx0, center_split_y), ImVec2(cx1, center_split_y + gap));
    if (bottom_open && right_live) corner_center_right = _corner_splitter("##corner_center_right", ImVec2(cx1, center_split_y), true, right_v_min, right_v_max, ImVec2(cx0, center_split_y), ImVec2(cx1, center_split_y + gap));

    _draw_center(ctx, ImVec2(cx0, body_y), ImVec2(cx1, body_max_y));

    if (left_live) {
        ImGui::SetCursorScreenPos(ImVec2(lx1, body_y));
        SplitterState s = imgui_splitter("##split_left", SplitAxis::X, ImVec2(gap, body_h), 0.0f);
        if (s.active) _apply_split_left(s.delta, s.activated, body_w);
    }
    if (right_live) {
        ImGui::SetCursorScreenPos(ImVec2(cx1, body_y));
        SplitterState s = imgui_splitter("##split_lr", SplitAxis::X, ImVec2(gap, body_h), 0.0f);
        if (s.active) _apply_split_right(s.delta, s.activated, body_w);
    }

    if (left_shown) _draw_column(ctx, left, dragging, ImVec2(lx0, body_y), ImVec2(lx1, body_max_y));
    if (right_shown) _draw_column(ctx, right, dragging, ImVec2(rx0, body_y), ImVec2(rx1, body_max_y));

    if (corner_left.active) {
        _apply_split_left(corner_left.delta.x, corner_left.activated, body_w);
        left.split_y += corner_left.delta.y / col_h;
    }
    if (corner_right.active) {
        _apply_split_right(corner_right.delta.x, corner_right.activated, body_w);
        right.split_y += corner_right.delta.y / col_h;
    }
    if (corner_center_left.active) {
        _apply_split_left(corner_center_left.delta.x, corner_center_left.activated, body_w);
        _apply_center_delta(corner_center_left.delta.y, corner_center_left.activated);
    }
    if (corner_center_right.active) {
        _apply_split_right(corner_center_right.delta.x, corner_center_right.activated, body_w);
        _apply_center_delta(corner_center_right.delta.y, corner_center_right.activated);
    }

    if (drawer_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) _end_drawer_drag();

    if (dragging) {
        const float size = ui.dock.edge_target_size;
        const float margin = ui.dock.edge_target_margin;
        const float cy = ImFloor(body_y + body_h * 0.5f);
        const float half_gap = ImFloor(gap * 0.5f);
        if (!left_shown) {
            _edge_target("##edge_left_top", ImVec2(origin.x + margin, cy - half_gap - size), DockZone::LEFT_TOP);
            _edge_target("##edge_left_bottom", ImVec2(origin.x + margin, cy + half_gap), DockZone::LEFT_BOTTOM);
        }
        if (!right_shown) {
            _edge_target("##edge_right_top", ImVec2(end.x - margin - size, cy - half_gap - size), DockZone::RIGHT_TOP);
            _edge_target("##edge_right_bottom", ImVec2(end.x - margin - size, cy + half_gap), DockZone::RIGHT_BOTTOM);
        }
        if (!bottom_open) _edge_target("##edge_bottom", ImVec2(ImFloor((cx0 + cx1 - size) * 0.5f), body_max_y - margin - size), DockZone::BOTTOM);
    }

    _draw_footer(ImVec2(origin.x, footer_y), end);

    for (DockWell* w : { &bottom, &left.top, &left.bottom, &right.top, &right.bottom }) {
        if (w->dropped) pending_drop = { w->dropped, w->zone };
    }
    _apply_drop();

    ImGui::End();
}

void EditorLayout::draw_menu()
{
    if (ImGui::BeginMenu("Panels")) {
        for (DockSlot& s : slots) {
            const bool was_open = s.open;
            ImGui::MenuItem(s.panel->title, nullptr, &s.open, s.panel->closable() && !s.panel->pinned());
            if (s.open && !was_open) open_panel(s);
        }
        ImGui::EndMenu();
    }
}

}