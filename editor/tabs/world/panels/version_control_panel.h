#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct VersionControlPanel : Panel
{
    const char* name() const override { return ICON_FA_EARTH_AMERICAS "  Version Control"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    void draw_contents(EditorContext&) override;
};
    
}