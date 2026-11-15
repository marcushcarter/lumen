#pragma once
#include <core/assets/lterrain.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct TerrainDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::TERRAIN;
    static constexpr const char* ICON = ICON_FA_MOUNTAIN_SUN;

    LTerrain terrain;
    LTerrain saved;

    const char* _type_name() const override { return "Terrain"; }

    bool dirty() const override { return terrain != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
