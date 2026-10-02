#pragma once
#include <editor/docking/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct WorldSettingsPanel : Panel
{
    const char* name() const override { return ICON_FA_EARTH_AMERICAS "  World Settings"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    void draw_contents(EditorContext&) override;
};
    
}