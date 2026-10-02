#pragma once
#include <editor/editor_context.h>
#include <imgui.h>

namespace lumen {

enum class DockZone { LEFT, RIGHT_TOP, RIGHT_BOTTOM };

inline const char* to_string(DockZone z) {
    switch (z) {
        case DockZone::LEFT: return "Left";
        case DockZone::RIGHT_TOP: return "RightTop";
        case DockZone::RIGHT_BOTTOM: return "RightBottom";
    }
    return "RightBottom";
}

inline DockZone dock_zone_from_string(std::string_view s, DockZone fallback) {
    if (s == "Left") return DockZone::LEFT;
    if (s == "RightTop") return DockZone::RIGHT_TOP;
    if (s == "RightBottom") return DockZone::RIGHT_BOTTOM;
    return fallback;
}

struct Panel
{
    bool open = true;
    DockZone zone = DockZone::RIGHT_BOTTOM;

    virtual ~Panel() = default;
    virtual const char* name() const = 0;
    virtual DockZone default_zone() const { return DockZone::RIGHT_BOTTOM; }

    virtual void draw_contents(EditorContext& ctx) = 0;
    virtual ImGuiWindowFlags window_flags() const { return 0; }
    virtual int push_style() { return 0; }
    virtual void before_begin() {}
};

}