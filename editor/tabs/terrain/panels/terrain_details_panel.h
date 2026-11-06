#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct TerrainTab;

struct TerrainDetailsPanel : Panel
{
    TerrainTab* tab = nullptr;

    const char* name() const override { return ICON_FA_MOUNTAIN_SUN "  Terrain"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }

    void draw_contents(EditorContext& ctx) override;
};

}
