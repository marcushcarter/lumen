#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <filesystem>

namespace lumen {

struct EditorContext;

struct TerrainCooker
{
    static constexpr float LOAD_SHARE = 0.1f;
    static constexpr float TILES_END = 0.95f;

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lterrain;
        std::filesystem::path content_bin;
        Guid guid;
        ImportStep step;
    };

    static Error write_cooked(const std::filesystem::path& p_bin, Guid p_guid, const uint16_t* p_samples, uint32_t p_w, uint32_t p_h, float p_spacing, float p_height_min, float p_height_max, const ImportStep* p_step, uint32_t& r_tiles_x, uint32_t& r_tiles_y);
    static Error _cook(const Job& p_job);
    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst);
};

}
