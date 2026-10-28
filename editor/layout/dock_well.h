#pragma once
#include <editor/layout/panel.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <string>

namespace lumen {

struct DockSlot
{
    Panel* panel = nullptr;
    DockZone zone = DockZone::RIGHT_BOTTOM;
    bool open = true;
};

struct DockWell
{
    static constexpr const char* PAYLOAD = "DOCK_PANEL";

    DockZone zone = DockZone::RIGHT_BOTTOM;
    std::vector<DockSlot*> slots;
    std::string active_name;
    Panel* dropped = nullptr;
    float tab_scroll = 0.0f;
    std::string revealed_name;

    bool has_open() const;
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    DockSlot* _resolve_active();
    void _drop_target(ImVec2 p_min, ImVec2 p_max);
};

}
