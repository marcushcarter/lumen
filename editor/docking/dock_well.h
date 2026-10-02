#pragma once
#include <editor/docking/panel.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <string>

namespace lumen {

struct DockWell
{
    DockZone zone = DockZone::RIGHT_BOTTOM;
    std::vector<Panel*> panels;
    std::string active_name;

    void draw(EditorContext& ctx)
    {
        const ImVec2 well_min = ImGui::GetCursorScreenPos();
        const ImVec2 well_size = ImGui::GetContentRegionAvail();
        const ImVec2 well_max = ImVec2(well_min.x + well_size.x, well_min.y + well_size.y);

        Panel* active = nullptr;
        Panel* first_open = nullptr;
        for (Panel* p : panels) {
            if (!p->open) continue;
            if (!first_open) first_open = p;
            if (active_name == p->name()) active = p;
        }
        if (!active) active = first_open;
        if (active) active_name = active->name();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad_x = 12.0f;
        const float tab_h = ImGui::GetFrameHeight();
        const float row_top = ImGui::GetCursorScreenPos().y;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
        bool first = true;
        for (Panel* p : panels) {
            if (!p->open) continue;
            if (!first) ImGui::SameLine();
            first = false;

            ImVec2 label = ImGui::CalcTextSize(p->name());
            ImVec2 size(label.x + pad_x * 2.0f, tab_h);
            ImVec2 pos = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton(p->name(), size);
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) { active = p; active_name = p->name(); }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("DOCK_PANEL", &p, sizeof(Panel*));
                ImGui::TextUnformatted(p->name());
                ImGui::EndDragDropSource();
            }

            bool sel = (p == active);
            ImU32 fill = ImGui::GetColorU32(sel ? ImGuiCol_TabActive : hovered ? ImGuiCol_TabHovered : ImGuiCol_Tab);
            ImVec2 mn = pos, mx(pos.x + size.x, pos.y + size.y);
            dl->AddRectFilled(mn, mx, fill, 5.0f, ImDrawFlags_RoundCornersTop);
            dl->AddText(ImVec2(mn.x + pad_x, mn.y + (size.y - label.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), p->name());
        }
        ImGui::PopStyleVar();

        float base_y = row_top + tab_h;
        dl->AddLine(ImVec2(well_min.x, base_y), ImVec2(well_max.x, base_y), ImGui::GetColorU32(ImGuiCol_TabActive), 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 4));
        ImGui::BeginChild("##wellcontent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
        if (active) active->draw_contents(ctx);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (ImGui::BeginDragDropTargetCustom(ImRect(well_min, well_max), ImGui::GetID("##dockwell_target"))) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("DOCK_PANEL", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                dl->AddRect(well_min, well_max, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 0, 2.0f);
                if (pl->IsDelivery()) {
                    Panel* dragged = *reinterpret_cast<Panel* const*>(pl->Data);
                    dragged->zone = zone;
                    active_name = dragged->name();
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
};

}