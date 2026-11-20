#include <core/terrain/terrain_source.h>
#include <core/assets/asset_common.h>
#include <core/scene/scene.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace lumen {

template <typename T>
static bool _read_block(std::ifstream& r_in, uint64_t p_offset, uint64_t p_bytes, std::vector<T>& r_out)
{
    r_out.resize((size_t)(p_bytes / sizeof(T)));
    r_in.seekg((std::streamoff)p_offset);
    r_in.read(reinterpret_cast<char*>(r_out.data()), (std::streamsize)p_bytes);
    return (bool)r_in;
}

Error TerrainSource::open(Guid p_guid, const std::filesystem::path& p_path)
{
    using enum Error;
    close();
    std::ifstream in(p_path, std::ios::binary);
    if (!in) return FAILED;
    LAssetHeader ah{};
    in.read(reinterpret_cast<char*>(&ah), sizeof(ah));
    if (!in || ah.magic != BCON_MAGIC || ah.type != AssetType::TERRAIN) return FAILED;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || header.format_version != LTERRAIN_FORMAT_VERSION || header.tile_quads != LTERRAIN_TILE_QUADS || header.lod_count != LTERRAIN_LOD_COUNT || header.apron != LTERRAIN_APRON) return FAILED;
    if (header.tiles_x == 0 || header.tiles_y == 0 || header.bounds_nodes != lterrain_bounds_nodes()) return FAILED;

    std::vector<uint16_t> b, c;
    std::vector<LTerrainTileEntry> d;
    if (!_read_block(in, header.bounds_offset, header.bounds_size, b)) return FAILED;
    if (!_read_block(in, header.coarse_offset, header.coarse_size, c)) return FAILED;
    if (!_read_block(in, header.directory_offset, header.directory_size, d)) return FAILED;
    if (d.size() != tile_count()) return FAILED;

    guid = p_guid;
    path = p_path;
    bounds = std::move(b);
    coarse = std::move(c);
    tiles = std::move(d);
    uint64_t offset = 0;
    uint32_t base = 0;
    for (uint32_t l = 0; l < LTERRAIN_LOD_COUNT; l++) {
        lod_offsets[l] = offset;
        offset += (uint64_t)lterrain_lod_samples(l) * lterrain_lod_samples(l) * sizeof(uint16_t);
        bounds_level_base[l] = base;
        base += (1u << l) * (1u << l);
    }
    return OK;
}

void TerrainSource::close()
{
    guid = {};
    path.clear();
    header = {};
    bounds.clear();
    coarse.clear();
    tiles.clear();
}

vec3 TerrainSource::origin() const
{
    return vec3(-0.5f * (float)(header.samples_x - 1) * header.spacing, 0.0f, -0.5f * (float)(header.samples_y - 1) * header.spacing);
}

float TerrainSource::coarse_height(float p_sample_x, float p_sample_y) const
{
    if (coarse.empty()) return 0.0f;
    const float fx = std::clamp(p_sample_x / (float)header.coarse_step, 0.0f, (float)(header.coarse_w - 1));
    const float fy = std::clamp(p_sample_y / (float)header.coarse_step, 0.0f, (float)(header.coarse_h - 1));
    const uint32_t x0 = std::min((uint32_t)fx, header.coarse_w - 2);
    const uint32_t y0 = std::min((uint32_t)fy, header.coarse_h - 2);
    const float tx = fx - (float)x0;
    const float ty = fy - (float)y0;
    const auto at = [&](uint32_t x, uint32_t y) { return (float)coarse[(size_t)y * header.coarse_w + x]; };
    const float h = (at(x0, y0) * (1.0f - tx) + at(x0 + 1, y0) * tx) * (1.0f - ty) + (at(x0, y0 + 1) * (1.0f - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
    return header.height_min + h * height_scale();
}

void TerrainSource::node_range(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, float& r_min, float& r_max) const
{
    const uint16_t* node = bounds.data() + 2 * ((size_t)p_tile * header.bounds_nodes + bounds_level_base[p_level] + p_j * (1u << p_level) + p_i);
    r_min = height_meters(node[0]);
    r_max = height_meters(node[1]);
}

bool TerrainSource::read_lod(uint32_t p_tile, uint32_t p_lod, uint16_t* r_samples) const
{
    if (p_tile >= tiles.size() || p_lod >= LTERRAIN_LOD_COUNT) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const uint64_t bytes = (uint64_t)lterrain_lod_samples(p_lod) * lterrain_lod_samples(p_lod) * sizeof(uint16_t);
    in.seekg((std::streamoff)(tiles[p_tile].offset + lod_offsets[p_lod]));
    in.read(reinterpret_cast<char*>(r_samples), (std::streamsize)bytes);
    return (bool)in;
}

}
