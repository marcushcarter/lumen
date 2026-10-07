#pragma once
#include <editor/docking/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct OutlinerPanel : Panel
{
    const char* name() const override { return ICON_FA_IMAGE "  Outliner"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    void draw_contents(EditorContext& ctx) override;
};

}