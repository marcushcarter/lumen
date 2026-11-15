#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct TerrainDocument;

struct TerrainDetailsPanel : Panel
{
    TerrainDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_MOUNTAIN_SUN "  Terrain"; }

    void draw_contents(EditorContext& ctx) override;
};

}
