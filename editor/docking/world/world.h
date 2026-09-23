#pragma once
#include <editor/docking/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct WorldPanel : Panel
{
    const char* name() const override { return ICON_FA_EARTH_AMERICAS "  World"; }
    DockZone default_zone() const override { return DockZone::RightTop; }
    void draw_contents(EditorContext&) override;
};
    
}