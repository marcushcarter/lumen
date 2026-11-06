#pragma once
#include <core/assets/lterrain.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct TerrainTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::TERRAIN;
    static constexpr const char* ICON = ICON_FA_MOUNTAIN_SUN;

    LTerrain terrain;
    LTerrain saved;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_terrain; }

    const char* title() const override { return _title.empty() ? "Terrain" : _title.c_str(); }
    bool dirty() const override { return terrain != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
