#include <editor/assets/import/terrain_cooker.h>
#include <core/assets/asset_common.h>
#include <core/assets/lterrain.h>
#include <core/base/jobs.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <editor/application/editor_context.h>
#include <stb_image.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace lumen {

struct HeightGrid
{
    const uint16_t* data = nullptr;
    int64_t width = 0;
    int64_t height = 0;

    uint16_t at(int64_t p_x, int64_t p_y) const
    {
        const int64_t x = std::clamp<int64_t>(p_x, 0, width - 1);
        const int64_t y = std::clamp<int64_t>(p_y, 0, height - 1);
        return data[y * width + x];
    }
};

static uint32_t _morton(uint32_t p_x, uint32_t p_y)
{
    uint32_t m = 0;
    for (uint32_t b = 0; b < 16; b++) m |= ((p_x >> b) & 1u) << (2 * b) | ((p_y >> b) & 1u) << (2 * b + 1);
    return m;
}

static uint64_t _align(uint64_t p_value, uint64_t p_align)
{
    return (p_value + p_align - 1) / p_align * p_align;
}

static void _tile_bounds(const HeightGrid& p_grid, int64_t p_ox, int64_t p_oy, uint16_t* r_bounds)
{
    uint32_t level_base[LTERRAIN_LOD_COUNT];
    uint32_t base = 0;
    for (uint32_t l = 0; l < LTERRAIN_LOD_COUNT; l++) {
        level_base[l] = base;
        base += (1u << l) * (1u << l);
    }

    const uint32_t leaf = LTERRAIN_LOD_COUNT - 1;
    const uint32_t leaf_n = 1u << leaf;
    const int64_t leaf_q = LTERRAIN_TILE_QUADS >> leaf;
    for (uint32_t j = 0; j < leaf_n; j++) {
        for (uint32_t i = 0; i < leaf_n; i++) {
            uint16_t lo = UINT16_MAX;
            uint16_t hi = 0;
            for (int64_t y = 0; y <= leaf_q; y++) {
                for (int64_t x = 0; x <= leaf_q; x++) {
                    const uint16_t h = p_grid.at(p_ox + (int64_t)i * leaf_q + x, p_oy + (int64_t)j * leaf_q + y);
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            }
            uint16_t* node = r_bounds + 2 * (level_base[leaf] + j * leaf_n + i);
            node[0] = lo;
            node[1] = hi;
        }
    }

    for (uint32_t l = leaf; l-- > 0;) {
        const uint32_t n = 1u << l;
        const uint32_t cn = n * 2;
        for (uint32_t j = 0; j < n; j++) {
            for (uint32_t i = 0; i < n; i++) {
                uint16_t lo = UINT16_MAX;
                uint16_t hi = 0;
                for (uint32_t c = 0; c < 4; c++) {
                    const uint16_t* child = r_bounds + 2 * (level_base[l + 1] + (j * 2 + (c >> 1)) * cn + i * 2 + (c & 1));
                    lo = std::min(lo, child[0]);
                    hi = std::max(hi, child[1]);
                }
                uint16_t* node = r_bounds + 2 * (level_base[l] + j * n + i);
                node[0] = lo;
                node[1] = hi;
            }
        }
    }
}

Error TerrainCooker::write_cooked(const std::filesystem::path& p_bin, Guid p_guid, const uint16_t* p_samples, uint32_t p_w, uint32_t p_h, float p_spacing, float p_height_min, float p_height_max, const ImportStep* p_step, uint32_t& r_tiles_x, uint32_t& r_tiles_y)
{
    using enum Error;
    const HeightGrid grid{ p_samples, (int64_t)p_w, (int64_t)p_h };
    const uint32_t w = p_w;
    const uint32_t h = p_h;
    const uint32_t tiles_x = std::max(1u, (w - 1 + LTERRAIN_TILE_QUADS - 1) / LTERRAIN_TILE_QUADS);
    const uint32_t tiles_y = std::max(1u, (h - 1 + LTERRAIN_TILE_QUADS - 1) / LTERRAIN_TILE_QUADS);
    const uint32_t tile_count = tiles_x * tiles_y;
    const uint32_t coarse_w = tiles_x * LTERRAIN_TILE_QUADS / LTERRAIN_COARSE_STEP + 1;
    const uint32_t coarse_h = tiles_y * LTERRAIN_TILE_QUADS / LTERRAIN_COARSE_STEP + 1;
    const uint32_t bounds_nodes = lterrain_bounds_nodes();

    uint64_t lod_samples = 0;
    for (uint32_t l = 0; l < LTERRAIN_LOD_COUNT; l++) lod_samples += (uint64_t)lterrain_lod_samples(l) * lterrain_lod_samples(l);
    const uint32_t height_size = (uint32_t)(lod_samples * sizeof(uint16_t));
    const uint64_t tile_stride = _align(height_size, LTERRAIN_PAGE_ALIGN);

    LTerrainPayloadHeader ph{};
    ph.format_version = LTERRAIN_FORMAT_VERSION;
    ph.tile_quads = LTERRAIN_TILE_QUADS;
    ph.apron = LTERRAIN_APRON;
    ph.lod_count = LTERRAIN_LOD_COUNT;
    ph.tiles_x = tiles_x;
    ph.tiles_y = tiles_y;
    ph.samples_x = w;
    ph.samples_y = h;
    ph.spacing = p_spacing;
    ph.height_min = p_height_min;
    ph.height_max = p_height_max;
    ph.layer_count = 0;
    ph.coarse_step = LTERRAIN_COARSE_STEP;
    ph.coarse_w = coarse_w;
    ph.coarse_h = coarse_h;
    ph.bounds_nodes = bounds_nodes;
    ph.bounds_offset = sizeof(LAssetHeader) + sizeof(LTerrainPayloadHeader);
    ph.bounds_size = (uint64_t)tile_count * bounds_nodes * 2 * sizeof(uint16_t);
    ph.coarse_offset = ph.bounds_offset + ph.bounds_size;
    ph.coarse_size = (uint64_t)coarse_w * coarse_h * sizeof(uint16_t);
    ph.directory_offset = ph.coarse_offset + ph.coarse_size;
    ph.directory_size = (uint64_t)tile_count * sizeof(LTerrainTileEntry);
    const uint64_t tiles_offset = _align(ph.directory_offset + ph.directory_size, LTERRAIN_PAGE_ALIGN);
    const uint64_t total = tiles_offset + tile_stride * tile_count;
    if (total - sizeof(LAssetHeader) > UINT32_MAX) {
        log_write("Terrain: '%s' is too large for one terrain file.", p_bin.string().c_str());
        return FAILED;
    }

    std::vector<uint8_t> bin((size_t)total, 0);
    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LASSET_VERSION;
    ah.guid = p_guid;
    ah.type = AssetType::TERRAIN;
    ah.payload_size = (uint32_t)(total - sizeof(LAssetHeader));
    std::memcpy(bin.data(), &ah, sizeof(ah));
    std::memcpy(bin.data() + sizeof(ah), &ph, sizeof(ph));

    uint16_t* coarse = reinterpret_cast<uint16_t*>(bin.data() + ph.coarse_offset);
    for (uint32_t j = 0; j < coarse_h; j++) {
        for (uint32_t i = 0; i < coarse_w; i++) coarse[j * coarse_w + i] = grid.at((int64_t)i * LTERRAIN_COARSE_STEP, (int64_t)j * LTERRAIN_COARSE_STEP);
    }

    std::vector<uint32_t> order(tile_count);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return _morton(a % tiles_x, a / tiles_x) < _morton(b % tiles_x, b / tiles_x); });

    LTerrainTileEntry* directory = reinterpret_cast<LTerrainTileEntry*>(bin.data() + ph.directory_offset);
    uint16_t* bounds = reinterpret_cast<uint16_t*>(bin.data() + ph.bounds_offset);
    for (uint32_t slot = 0; slot < tile_count; slot++) {
        const uint32_t t = order[slot];
        const int64_t ox = (int64_t)(t % tiles_x) * LTERRAIN_TILE_QUADS;
        const int64_t oy = (int64_t)(t / tiles_x) * LTERRAIN_TILE_QUADS;
        const uint64_t offset = tiles_offset + tile_stride * slot;
        directory[t] = { offset, height_size, 0u, 0u, 0u };

        uint16_t* out = reinterpret_cast<uint16_t*>(bin.data() + offset);
        for (uint32_t l = 0; l < LTERRAIN_LOD_COUNT; l++) {
            const int64_t stride = (int64_t)1 << l;
            const uint32_t s = lterrain_lod_samples(l);
            for (uint32_t j = 0; j < s; j++) {
                for (uint32_t i = 0; i < s; i++) *out++ = grid.at(ox + ((int64_t)i - (int64_t)LTERRAIN_APRON) * stride, oy + ((int64_t)j - (int64_t)LTERRAIN_APRON) * stride);
            }
        }
        _tile_bounds(grid, ox, oy, bounds + (size_t)t * bounds_nodes * 2);

        if (p_step) {
            p_step->report((float)(slot + 1) / (float)tile_count);
            if (p_step->cancelled()) return CANCELED;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(p_bin.parent_path(), ec);
    if (!write_file_atomic(p_bin, bin.data(), bin.size())) return FAILED;
    r_tiles_x = tiles_x;
    r_tiles_y = tiles_y;
    return OK;


}

Error TerrainCooker::_cook(const Job& p_job)
{
    using enum Error;
    const ImportStep& step = p_job.step;

    FILE* file = nullptr;
    if (_wfopen_s(&file, p_job.source.wstring().c_str(), L"rb") != 0 || !file) return FAILED;
    const bool sixteen = stbi_is_16_bit_from_file(file) != 0;
    int w = 0, h = 0, channels = 0;
    stbi_us* pixels = stbi_load_from_file_16(file, &w, &h, &channels, 1);
    std::fclose(file);
    if (!pixels) return FAILED;
    if (w < 2 || h < 2) {
        stbi_image_free(pixels);
        return FAILED;
    }
    if (!sixteen) log_write("Terrain import: '%s' is 8-bit. Heights will show visible steps; export 16-bit for smooth terrain.", p_job.source.string().c_str());
    step.report(LOAD_SHARE);
    if (step.cancelled()) {
        stbi_image_free(pixels);
        return CANCELED;
    }

    uint32_t tiles_x = 0, tiles_y = 0;
    const ImportStep tile_step = step.sub(LOAD_SHARE, TILES_END);
    const Error err = write_cooked(p_job.content_bin, p_job.guid, pixels, (uint32_t)w, (uint32_t)h, LTERRAIN_DEFAULT_SPACING, LTERRAIN_DEFAULT_HEIGHT_MIN, LTERRAIN_DEFAULT_HEIGHT_MAX, &tile_step, tiles_x, tiles_y);
    stbi_image_free(pixels);
    if (err != OK) return err;

    LTerrain terrain;
    terrain.guid = p_job.guid;
    terrain.source = p_job.source.generic_string();
    terrain.source_width = (uint32_t)w;
    terrain.source_height = (uint32_t)h;
    terrain.source_bits = sixteen ? 16u : 8u;
    terrain.samples_x = (uint32_t)w;
    terrain.samples_y = (uint32_t)h;
    terrain.tiles_x = tiles_x;
    terrain.tiles_y = tiles_y;
    if (terrain.save(p_job.dst_lterrain) != OK) return FAILED;

    step.report(1.0f);
    return OK;
}

void TerrainCooker::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst)
{
    Job job;
    job.source = p_src;
    std::filesystem::path content_bin;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::TERRAIN, p_dst, job.guid, content_bin) != Error::OK) {
        log_write("Terrain import failed: %s", p_src.string().c_str());
        return;
    }
    std::shared_ptr<ImportSession> session = ctx.imports->begin(*ctx.project, p_dst);
    if (!session) return;
    const ImportSession::Staged staged = session->stage(*ctx.project, p_dst, job.guid, true, true);
    job.dst_lterrain = staged.asset;
    job.content_bin = staged.content_bin;
    job.step = session->step();
    job_system().run_background_owned([job, session]{
        session->finish(TerrainCooker::_cook(job));
    }, nullptr, "Terrain Import");
}

}
