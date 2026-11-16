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

    static constexpr float NEAR_SCALE = 0.0002f;
    static constexpr float MIN_SPACING = 0.05f;
    static constexpr float MAX_SPACING = 100.0f;
    static constexpr float MAX_HEIGHT = 20000.0f;
    static constexpr float MIN_HEIGHT_RANGE = 0.1f;
    static constexpr float FRAME_PITCH = 0.6f;
    static constexpr uint32_t GENERATE_SIZES[] = { 257, 513, 1025, 2049, 4097, 8193 };
    static constexpr uint32_t GENERATE_DEFAULT_SIZE = 3;
    static constexpr float GENERATE_HEIGHT_MIN = -256.0f;
    static constexpr float GENERATE_HEIGHT_MAX = 768.0f;

    LTerrain terrain;
    LTerrain saved;
    uint32_t generate_size = GENERATE_DEFAULT_SIZE;
    float generate_spacing = LTERRAIN_DEFAULT_SPACING;
    float generate_height_min = GENERATE_HEIGHT_MIN;
    float generate_height_max = GENERATE_HEIGHT_MAX;

    const char* _type_name() const override { return "Terrain"; }

    bool dirty() const override { return terrain != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
    void _frame_terrain(EditorContext& ctx);
    Error _write_header(EditorContext& ctx) const;
    bool has_data() const { return terrain.samples_x && terrain.samples_y; }
    void generate_flat(EditorContext& ctx);
    void clear_terrain(EditorContext& ctx);
    void _apply_new_data(EditorContext& ctx);
};

}
