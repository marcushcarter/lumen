#pragma once
#include <editor/editor_context.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <string_view>

namespace lumen {

enum class DockZone { BOTTOM, LEFT_TOP, LEFT_BOTTOM, RIGHT_TOP, RIGHT_BOTTOM };

inline const char* to_string(DockZone z) {
    switch (z) {
        case DockZone::BOTTOM: return "Bottom";
        case DockZone::LEFT_TOP: return "LeftTop";
        case DockZone::LEFT_BOTTOM: return "LeftBottom";
        case DockZone::RIGHT_TOP: return "RightTop";
        case DockZone::RIGHT_BOTTOM: return "RightBottom";
    }
    return "RightBottom";
}

inline DockZone dock_zone_from_string(std::string_view s, DockZone fallback) {
    if (s == "Bottom") return DockZone::BOTTOM;
    if (s == "LeftTop") return DockZone::LEFT_TOP;
    if (s == "LeftBottom") return DockZone::LEFT_BOTTOM;
    if (s == "RightTop") return DockZone::RIGHT_TOP;
    if (s == "RightBottom") return DockZone::RIGHT_BOTTOM;
    return fallback;
}

struct Panel
{
    virtual ~Panel() = default;
    virtual const char* name() const = 0;
    virtual DockZone default_zone() const { return DockZone::RIGHT_BOTTOM; }
    virtual bool default_open() const { return true; }
    virtual bool closable() const { return true; }
    virtual bool in_footer() const { return false; }
    virtual bool pinned() const { return false; }

    virtual void draw_contents(EditorContext& ctx) = 0;
    virtual ImGuiWindowFlags window_flags() const { return 0; }
    virtual ImVec2 window_padding() const { return ui_style().dock.panel_padding; }
    virtual int push_style() { return 0; }
    virtual void before_begin() {}
    virtual void shutdown() {}
};

}