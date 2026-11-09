#include <editor/layout/dock_well.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui_internal.h>

namespace lumen {

bool DockWell::has_open() const
{
    for (const DockSlot* s : slots) if (s->open) return true;
    return false;
}

DockSlot* DockWell::_resolve_active()
{
    DockSlot* active = nullptr;
    DockSlot* first_open = nullptr;
    for (DockSlot* s : slots) {
        if (!s->open) continue;
        if (!first_open) first_open = s;
        if (active_name == s->panel->layout_id) active = s;
    }
    if (!active) active = first_open;
    if (active) active_name = active->panel->layout_id;
    return active;
}

void DockWell::_drop_target(ImVec2 p_min, ImVec2 p_max)
{
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (!peek || !peek->IsDataType(PAYLOAD)) return;
    Panel* dragged = *reinterpret_cast<Panel* const*>(peek->Data);
    for (const DockSlot* s : slots) if (s->panel == dragged && s->open) return;

    if (!ImGui::BeginDragDropTargetCustom(ImRect(p_min, p_max), ImGui::GetID("##drop"))) return;
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (pl->IsPreview()) {
            const UiStyle& ui = ui_style();
            ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
            fg->AddRectFilled(p_min, p_max, ui.color.drop_fill);
            fg->AddRect(p_min, p_max, ui.color.drop_line, 0.0f, 0, ui.dock.drop_outline);
        }
        if (pl->IsDelivery()) dropped = dragged;
    }
    ImGui::EndDragDropTarget();
}

void DockWell::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    DockSlot* active = _resolve_active();
    const UiStyle& ui = ui_style();
    const float tab_h = TabStrip::dock_height();
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);

    ImGui::PushID(to_string(zone));

    float natural = 0.0f;
    ImVector<float> naturals;
    ImVector<float> mins;
    for (DockSlot* s : slots) {
        if (!s->open) continue;
        naturals.push_back(TabStrip::dock_natural_width(s->panel->title, s->panel->closable()));
        mins.push_back(TabStrip::dock_min_width(s->panel->closable()));
        natural += naturals.back();
    }

    DockSlot* closed = nullptr;
    TabStrip strip;
    strip.rounding = ui.panel_box.rounding;
    strip.round_flags = ImDrawFlags_RoundCornersTop;
    strip.begin("##tabs", p_min, ImVec2(p_max.x, p_min.y + tab_h), TabStrip::Edge::TOP, natural, ui.dock_tab.inset_x * 2.0f);
    strip.fit(naturals, mins);
    strip.cursor_x += ui.dock_tab.inset_x;
    for (DockSlot* s : slots) {
        if (!s->open) continue;
        Panel* p = s->panel;
        bool close = false;
        if (strip.dock_tab(p->title, s == active, p->closable() ? &close : nullptr)) {
            active = s;
            active_name = p->layout_id;
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
            ImGui::SetDragDropPayload(PAYLOAD, &p, sizeof(Panel*));
            ImGui::TextUnformatted(p->title);
            ImGui::EndDragDropSource();
        }
        if (close) closed = s;
    }
    strip.end();

    if (closed) {
        closed->open = false;
        if (closed == active) active = _resolve_active();
    }

    const float content_h = ImMax(1.0f, size.y - tab_h);
    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + tab_h));
    ImGui::PushID(active ? active->panel->layout_id : "##empty");
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p_min.x, p_min.y + tab_h), p_max, ui.color.panel, ui.panel_box.rounding, ImDrawFlags_RoundCornersBottom);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, active ? active->panel->window_padding() : ui.dock.panel_padding);
    const ImGuiWindowFlags flags = (active ? active->panel->window_flags() : 0) | ImGuiWindowFlags_NoScrollbar;
    const bool visible = ImGui::BeginChild("##content", ImVec2(size.x, content_h), ImGuiChildFlags_AlwaysUseWindowPadding, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (visible) {
        if (active) {
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
            active->panel->draw_contents(ctx);
            ui_category_flush();
            ImGui::PopStyleVar();
        } else {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            ui_text_centered(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ui.color.text_dim, "Drop a panel here");
        }
    }
    ImGui::EndChild();
    ImGui::PopID();

    _drop_target(p_min, p_max);

    ImGui::PopID();
}

}
