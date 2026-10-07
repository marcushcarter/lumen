#pragma once
#include <editor/docking/panel.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <string>

namespace lumen {

struct DockWell
{
    static constexpr const char* PAYLOAD = "DOCK_PANEL";

    DockZone zone = DockZone::RIGHT_BOTTOM;
    std::vector<Panel*> panels;
    std::string active_name;

    bool has_open() const;
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    Panel* _resolve_active();
    void _drop_target(ImVec2 p_min, ImVec2 p_max);
};

}
