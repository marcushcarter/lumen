#include <editor/docking/dock_well.h>
#include <editor/docking/tab_strip.h>
#include <imgui_internal.h>

namespace lumen {

bool DockWell::has_open() const
{
    for (const Panel* p : panels) if (p->open) return true;
    return false;
}

Panel* DockWell::_resolve_active()
{
    Panel* active = nullptr;
    Panel* first_open = nullptr;
    for (Panel* p : panels) {
        if (!p->open) continue;
        if (!first_open) first_open = p;
        if (active_name == p->name()) active = p;
    }
    if (!active) active = first_open;
    if (active) active_name = active->name();
    return active;
}

void DockWell::_drop_target(ImVec2 p_min, ImVec2 p_max)
{
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (!peek || !peek->IsDataType(PAYLOAD)) return;
    Panel* dragged = *reinterpret_cast<Panel* const*>(peek->Data);
    if (dragged->zone == zone) return;

    if (!ImGui::BeginDragDropTargetCustom(ImRect(p_min, p_max), ImGui::GetID("##drop"))) return;
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        const DockColors colors = DockColors::get();
        ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        fg->AddRectFilled(p_min, p_max, colors.drop_fill);
        fg->AddRect(p_min, p_max, colors.drop_line, 0.0f, 0, 2.0f);
        if (pl->IsDelivery()) {
            dragged->zone = zone;
            active_name = dragged->name();
        }
    }
    ImGui::EndDragDropTarget();
}

void DockWell::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    Panel* active = _resolve_active();
    const DockColors colors = DockColors::get();
    const float tab_h = TabStrip::height();
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);

    ImGui::PushID(to_string(zone));

    float natural = 0.0f;
    for (Panel* p : panels) if (p->open) natural += TabStrip::natural_width(p->name());

    TabStrip strip;
    strip.begin("##tabs", p_min, ImVec2(p_max.x, p_min.y + tab_h), TabStrip::Edge::TOP, natural);
    for (Panel* p : panels) {
        if (!p->open) continue;
        if (strip.tab(p->name(), p == active)) {
            active = p;
            active_name = p->name();
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
            ImGui::SetDragDropPayload(PAYLOAD, &p, sizeof(Panel*));
            ImGui::TextUnformatted(p->name());
            ImGui::EndDragDropSource();
        }
    }
    strip.end();

    const float content_h = ImMax(1.0f, size.y - tab_h);
    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + tab_h));
    ImGui::PushID(active ? active->name() : "##empty");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.pane);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, active ? active->window_padding() : ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    const bool visible = ImGui::BeginChild("##content", ImVec2(size.x, content_h), ImGuiChildFlags_AlwaysUseWindowPadding, active ? active->window_flags() : ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (visible) {
        if (active) {
            active->draw_contents(ctx);
        } else {
            const char* hint = "Drop a panel here";
            const ImVec2 ts = ImGui::CalcTextSize(hint);
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddText(ImVec2(ImFloor(wp.x + (ws.x - ts.x) * 0.5f), ImFloor(wp.y + (ws.y - ts.y) * 0.5f)), colors.text_dim, hint);
        }
    }
    ImGui::EndChild();
    ImGui::PopID();

    _drop_target(p_min, p_max);

    ImGui::PopID();
}

}
