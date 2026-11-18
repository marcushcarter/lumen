#include <editor/layout/editor_layout.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace lumen {

static constexpr const char* ZONE_LABELS[] = { "Left Top", "Left Bottom", "Right Top", "Right Bottom", "Bottom" };

const char* to_string(DockZone p_zone)
{
    switch (p_zone) {
        case DockZone::LEFT_TOP: return "LeftTop";
        case DockZone::LEFT_BOTTOM: return "LeftBottom";
        case DockZone::RIGHT_TOP: return "RightTop";
        case DockZone::RIGHT_BOTTOM: return "RightBottom";
        case DockZone::BOTTOM: return "Bottom";
        case DockZone::COUNT: break;
    }
    return "LeftTop";
}

DockZone dock_zone_from_string(std::string_view p_name, DockZone p_fallback)
{
    for (size_t i = 0; i < EditorLayout::ZONES; i++) {
        if (p_name == to_string((DockZone)i)) return (DockZone)i;
    }
    return p_fallback;
}

void EditorLayout::add(Panel* p_panel, DockZone p_zone, bool p_open)
{
    slots.push_back({ p_panel, p_zone, p_open });
}

void EditorLayout::set_document(const std::vector<Slot>& p_slots, bool p_locked)
{
    document_slots = p_slots;
    document_locked = p_locked;
}

DockZone EditorLayout::document_zone(const Panel* p_panel, DockZone p_fallback) const
{
    const auto it = document_zones.find(p_panel->name());
    return it == document_zones.end() ? p_fallback : it->second;
}

void EditorLayout::open(Panel* p_panel)
{
    for (Slot& s : slots) {
        if (s.panel != p_panel) continue;
        s.open = true;
        active[(size_t)s.zone] = s.panel->name();
    }
}

bool EditorLayout::is_open(const Panel* p_panel) const
{
    for (const Slot& s : slots) if (s.panel == p_panel) return s.open;
    return false;
}

EditorLayout::Slot* EditorLayout::find(std::string_view p_name)
{
    for (Slot& s : slots) if (p_name == s.panel->name()) return &s;
    return nullptr;
}

bool EditorLayout::has(DockZone p_zone) const
{
    for (const Slot& s : slots) if (s.open && s.zone == p_zone) return true;
    for (const Slot& s : document_slots) if (s.open && s.zone == p_zone) return true;
    return false;
}

Panel* EditorLayout::_active(DockZone p_zone)
{
    Panel* first = nullptr;
    Panel* found = nullptr;
    std::string& name = active[(size_t)p_zone];
    _each([&](Slot& s) {
        if (s.zone != p_zone || found) return;
        if (!first) first = s.panel;
        if (name == s.panel->name()) found = s.panel;
    });
    if (found) return found;
    if (first) name = first->name();
    return first;
}

void EditorLayout::_draw_well(EditorContext& ctx, DockZone p_zone, ImVec2 p_min, ImVec2 p_max)
{
    Panel* current = _active(p_zone);
    if (!current) return;
    const float header_h = panel_header_height();
    ImGui::PushID(to_string(p_zone));

    const ImVec2 header_max(p_max.x, p_min.y + header_h);
    uint32_t count = 0;
    Panel* panels[MAX_PER_WELL] = {};
    PanelTab tabs[MAX_PER_WELL];
    bool document = false;
    _each([&](Slot& s) {
        if (s.zone != p_zone || count >= MAX_PER_WELL) return;
        panels[count] = s.panel;
        tabs[count].name = s.panel->name();
        tabs[count].icon = s.panel->icon();
        tabs[count].active = s.panel == current;
        if (s.panel == current) document = &s >= document_slots.data() && &s < document_slots.data() + document_slots.size();
        count++;
    });
    const PanelTabResult r = panel_tab_bar(tabs, (int)count, p_min, header_max, true);
    if (r.clicked >= 0) active[(size_t)p_zone] = panels[r.clicked]->name();
    if (r.menu) ImGui::OpenPopup("##well_menu");

    ui_menu_push_style();
    if (ImGui::BeginPopup("##well_menu")) {
        for (size_t z = 0; z < ZONES; z++) {
            if (ImGui::MenuItem(ZONE_LABELS[z], nullptr, (DockZone)z == p_zone) && (DockZone)z != p_zone) pending = { current, (DockZone)z };
        }
        if (current->closable()) {
            ImGui::Separator();
            if (ImGui::MenuItem("Close")) close_request = current;
        }
        ImGui::EndPopup();
    }
    ui_menu_pop_style();

    const bool lock = document && document_locked;
    if (lock) ImGui::BeginDisabled();
    panel_body(ctx, *current, ImVec2(p_min.x, header_max.y), p_max);
    if (lock) ImGui::EndDisabled();
    ImGui::PopID();
}

void EditorLayout::_draw_column(EditorContext& ctx, DockZone p_top, DockZone p_bottom, float& r_split, ImVec2 p_min, ImVec2 p_max, const char* p_id)
{
    const bool top = has(p_top);
    const bool bottom = has(p_bottom);
    if (!(top && bottom)) {
        _draw_well(ctx, top ? p_top : p_bottom, p_min, p_max);
        return;
    }
    const float h = ImMax(1.0f, p_max.y - p_min.y);
    const float min_r = ImMin(0.5f, MIN_SIDE_PX / h);
    r_split = ImClamp(r_split, min_r, 1.0f - min_r);
    float split = p_min.y + ImFloor(h * r_split);
    const SplitterState s = ui_edge_splitter(p_id, SplitAxis::Y, ImVec2(p_min.x, split), ImVec2(p_max.x, split));
    if (s.active) {
        r_split = ImClamp(r_split + s.delta / h, min_r, 1.0f - min_r);
        split = p_min.y + ImFloor(h * r_split);
    }
    _draw_well(ctx, p_top, p_min, ImVec2(p_max.x, split));
    _draw_well(ctx, p_bottom, ImVec2(p_min.x, split), p_max);
}

void EditorLayout::draw(EditorContext& ctx, const std::function<void(EditorContext&, ImVec2, ImVec2)>& p_toolbar)
{
    const UiStyle& ui = ui_style();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui.color.editor_border);
    ImGui::Begin("##EditorHost", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = ImGui::GetContentRegionAvail();
    const ImVec2 end(origin.x + extent.x, origin.y + extent.y);
    area_min = origin;
    area_max = end;

    float body_y = origin.y;
    if (p_toolbar) {
        const float bar_h = ui.toolbar.main_height;
        p_toolbar(ctx, origin, ImVec2(end.x, origin.y + bar_h));
        body_y += bar_h;
    }
    const float body_h = ImMax(1.0f, end.y - body_y);

    const bool left = has(DockZone::LEFT_TOP) || has(DockZone::LEFT_BOTTOM);
    const bool right = has(DockZone::RIGHT_TOP) || has(DockZone::RIGHT_BOTTOM);
    const bool bottom = has(DockZone::BOTTOM);

    const float avail_w = ImMax(1.0f, extent.x);
    const float max_side = ImMax(MIN_SIDE_PX, avail_w - MIN_CENTER_PX);
    const auto clamp_sides = [&] {
        if (right) right_w = ImClamp(right_w, MIN_SIDE_PX, ImMax(MIN_SIDE_PX, max_side - (left ? MIN_SIDE_PX : 0.0f)));
        if (left) left_w = ImClamp(left_w, MIN_SIDE_PX, ImMax(MIN_SIDE_PX, max_side - (right ? right_w : 0.0f)));
    };
    clamp_sides();
    if (left) {
        const float x = origin.x + ImFloor(left_w);
        const SplitterState sl = ui_edge_splitter("##split_left", SplitAxis::X, ImVec2(x, body_y), ImVec2(x, end.y));
        if (sl.active) left_w += sl.delta;
    }
    if (right) {
        const float x = end.x - ImFloor(right_w);
        const SplitterState sr = ui_edge_splitter("##split_right", SplitAxis::X, ImVec2(x, body_y), ImVec2(x, end.y));
        if (sr.active) right_w -= sr.delta;
    }
    clamp_sides();
    const float lx1 = origin.x + (left ? ImFloor(left_w) : 0.0f);
    const float rx0 = end.x - (right ? ImFloor(right_w) : 0.0f);
    const float cx0 = lx1;
    const float cx1 = rx0;

    float center_y1 = end.y;
    if (bottom) {
        bottom_h = ImClamp(bottom_h, MIN_SIDE_PX, ImMax(MIN_SIDE_PX, body_h - MIN_CENTER_PX));
        const float y = end.y - ImFloor(bottom_h);
        const SplitterState sb = ui_edge_splitter("##split_bottom", SplitAxis::Y, ImVec2(cx0, y), ImVec2(cx1, y));
        if (sb.active) bottom_h = ImClamp(bottom_h - sb.delta, MIN_SIDE_PX, ImMax(MIN_SIDE_PX, body_h - MIN_CENTER_PX));
        center_y1 = end.y - ImFloor(bottom_h);
    }

    if (center) center(ctx, ImVec2(cx0, body_y), ImVec2(cx1, center_y1));
    if (bottom) _draw_well(ctx, DockZone::BOTTOM, ImVec2(cx0, center_y1), ImVec2(cx1, end.y));
    if (left) _draw_column(ctx, DockZone::LEFT_TOP, DockZone::LEFT_BOTTOM, left_split_y, ImVec2(origin.x, body_y), ImVec2(lx1, end.y), "##left_split_y");
    if (right) _draw_column(ctx, DockZone::RIGHT_TOP, DockZone::RIGHT_BOTTOM, right_split_y, ImVec2(rx0, body_y), end, "##right_split_y");

    if (close_request) {
        for (Slot& s : slots) if (s.panel == close_request) s.open = false;
        close_request = nullptr;
    }

    if (pending.panel) {
        for (Slot& s : slots) {
            if (s.panel != pending.panel) continue;
            s.zone = pending.zone;
            active[(size_t)pending.zone] = s.panel->name();
        }
        for (Slot& s : document_slots) {
            if (s.panel != pending.panel) continue;
            s.zone = pending.zone;
            document_zones[s.panel->name()] = pending.zone;
            active[(size_t)pending.zone] = s.panel->name();
        }
        pending = {};
    }

    ImGui::End();
}

}
