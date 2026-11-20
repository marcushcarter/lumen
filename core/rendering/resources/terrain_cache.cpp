#include <core/rendering/resources/terrain_cache.h>
#include <core/base/jobs.h>
#include <core/base/hash.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>

namespace lumen {

static constexpr uint32_t MAX_COARSE_SAMPLES = 1025u * 1025u;
static constexpr uint32_t MAX_TILES = 64u * 64u;
static constexpr uint32_t COARSE_POOL_WORDS = MAX_COARSE_SAMPLES + 1;

static bool _read_range(const std::filesystem::path& p_path, uint64_t p_offset, uint64_t p_bytes, void* r_dst)
{
    std::ifstream in(p_path, std::ios::binary);
    if (!in) return false;
    in.seekg((std::streamoff)p_offset);
    in.read(static_cast<char*>(r_dst), (std::streamsize)p_bytes);
    return (bool)in;
}

static float _box_distance(const vec3& p_point, const vec3& p_min, const vec3& p_max)
{
    return length(glm::max(glm::max(p_min - p_point, vec3(0.0f)), p_point - p_max));
}

static bool _box_visible(const vec4* p_planes, const vec3& p_min, const vec3& p_max)
{
    for (uint32_t p = 0; p < 6; p++) {
        const vec3 n = vec3(p_planes[p]);
        const vec3 v(n.x >= 0.0f ? p_max.x : p_min.x, n.y >= 0.0f ? p_max.y : p_min.y, n.z >= 0.0f ? p_max.z : p_min.z);
        if (dot(n, v) + p_planes[p].w < 0.0f) return false;
    }
    return true;
}

Error TerrainCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count;

    const VkDeviceSize words = (VkDeviceSize)COARSE_POOL_WORDS + (VkDeviceSize)TERRAIN_FINE_SLOTS * FINE_SLOT_WORDS + (VkDeviceSize)TERRAIN_SMALL_SLOTS * SMALL_SLOT_WORDS;
    heights = dd->buffer_create({ .size = words * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = true, .name = "terrain_heights" });
    LUMEN_ERR_FAIL_COND_V(!heights.buffer, FAILED);

    staging.resize(frame_count);
    patch_buffers.resize(frame_count);
    tile_buffers.resize(frame_count);
    uniform_buffers.resize(frame_count);
    for (uint32_t i = 0; i < frame_count; i++) {
        staging[i] = dd->buffer_create({ .size = UPLOAD_BUDGET, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .device_local = false, .host_visible = true, .name = "terrain_staging" });
        patch_buffers[i] = dd->buffer_create({ .size = (VkDeviceSize)TERRAIN_MAX_PATCHES * sizeof(TerrainPatch), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "terrain_patches" });
        tile_buffers[i] = dd->buffer_create({ .size = (VkDeviceSize)MAX_TILES * sizeof(TerrainTileGpu), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "terrain_tiles" });
        uniform_buffers[i] = dd->buffer_create({ .size = sizeof(TerrainUniform), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "terrain_uniform" });
        LUMEN_ERR_FAIL_COND_V(!staging[i].buffer || !patch_buffers[i].buffer || !tile_buffers[i].buffer || !uniform_buffers[i].buffer, FAILED);
    }

    std::vector<uint16_t> indices;
    indices.reserve(TERRAIN_QUARTER_INDICES);
    const uint32_t row = TERRAIN_QUARTER_QUADS + 1;
    for (uint32_t j = 0; j < TERRAIN_QUARTER_QUADS; j++) {
        for (uint32_t i = 0; i < TERRAIN_QUARTER_QUADS; i++) {
            const uint16_t a = (uint16_t)(j * row + i);
            const uint16_t b = (uint16_t)(a + row);
            const uint16_t c = (uint16_t)(a + 1);
            const uint16_t d = (uint16_t)(b + 1);
            indices.insert(indices.end(), { a, b, c, c, b, d });
        }
    }
    quarter_indices = dd->buffer_create({ .size = indices.size() * sizeof(uint16_t), .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "terrain_indices" });
    LUMEN_ERR_FAIL_COND_V(!quarter_indices.buffer, FAILED);
    dd->buffer_update(quarter_indices, indices.data(), indices.size() * sizeof(uint16_t));
    dd->buffer_flush(quarter_indices, 0, indices.size() * sizeof(uint16_t));
    return OK;
}

void TerrainCache::shutdown()
{
    if (!dd) return;
    _deactivate();
    _requested = {};
    dd->buffer_free(heights);
    for (uint32_t i = 0; i < staging.size(); i++) {
        dd->buffer_free(staging[i]);
        dd->buffer_free(patch_buffers[i]);
        dd->buffer_free(tile_buffers[i]);
        dd->buffer_free(uniform_buffers[i]);
    }
    dd->buffer_free(quarter_indices);
    staging.clear();
    patch_buffers.clear();
    tile_buffers.clear();
    uniform_buffers.clear();
    paths.clear();
    dd = nullptr;
}

void TerrainCache::clear()
{
    _deactivate();
    _requested = {};
    paths.clear();
}

void TerrainCache::register_path(Guid p_guid, const std::filesystem::path& p_path)
{
    paths[p_guid.value] = p_path;
    if (_requested == p_guid) _requested = {};
}

void TerrainCache::reload(Guid p_guid, const std::filesystem::path& p_path)
{
    register_path(p_guid, p_path);
}

void TerrainCache::_activate(Guid p_terrain)
{
    _deactivate();
    _requested = p_terrain;
    const auto it = paths.find(p_terrain.value);
    if (it == paths.end()) return;
    if (source.open(p_terrain, it->second) != Error::OK) {
        log_write("Terrain: couldn't open '%s'.", it->second.string().c_str());
        return;
    }
    const LTerrainPayloadHeader& h = source.header;
    const uint32_t coarse_words = ((uint32_t)source.coarse.size() + 1) / 2;
    if ((uint64_t)h.coarse_w * h.coarse_h > MAX_COARSE_SAMPLES || source.tile_count() > MAX_TILES || coarse_words > COARSE_POOL_WORDS) {
        log_write("Terrain: '%s' is bigger than the streaming limits.", it->second.string().c_str());
        source.close();
        return;
    }
    guid = p_terrain;
    tiles.assign(source.tile_count(), Tile{});
    origin = source.origin();
    coarse_pending = true;
    const float base = (float)TERRAIN_PATCH_QUADS * h.spacing * LOD_RANGE_SCALE;
    for (uint32_t l = 0; l < TERRAIN_LEVELS; l++) ranges[l] = base * (float)(1u << l);

    tile_table.assign(source.tile_count(), TerrainTileGpu{ 0u, TERRAIN_LOD_NONE });
    free_fine.clear();
    free_small.clear();
    for (uint32_t i = TERRAIN_FINE_SLOTS; i-- > 0;) free_fine.push_back(i);
    for (uint32_t i = TERRAIN_SMALL_SLOTS; i-- > 0;) free_small.push_back(TERRAIN_FINE_SLOTS + i);
    retired.clear();
    height_lo = h.height_min;
    height_hi = h.height_max;
    signature = hash_combine(p_terrain.value, ++_activations);
}

void TerrainCache::_deactivate()
{
    frozen = false;
    for (const std::shared_ptr<Request>& r : inflight) {
        while (!r->done.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    inflight.clear();
    ready.clear();
    source.close();
    guid = {};
    tiles.clear();
    tile_table.clear();
    height_lo = height_hi = 0.0f;
    retired.clear();
    patch_count = 0;
    signature = 0;
}

VkDeviceSize TerrainCache::_slot_word(uint32_t p_slot) const
{
    if (p_slot < TERRAIN_FINE_SLOTS) return (VkDeviceSize)COARSE_POOL_WORDS + (VkDeviceSize)p_slot * FINE_SLOT_WORDS;
    return (VkDeviceSize)COARSE_POOL_WORDS + (VkDeviceSize)TERRAIN_FINE_SLOTS * FINE_SLOT_WORDS + (VkDeviceSize)(p_slot - TERRAIN_FINE_SLOTS) * SMALL_SLOT_WORDS;
}

bool TerrainCache::_stage(const void* p_data, VkDeviceSize p_bytes, VkDeviceSize p_dst)
{
    const VkDeviceSize size = (p_bytes + 3) & ~(VkDeviceSize)3;
    if (upload_used + size > UPLOAD_BUDGET) return false;
    drivers::DeviceDriverVulkan::Buffer& st = staging[upload_slot];
    std::memcpy(static_cast<uint8_t*>(st.mapped) + upload_used, p_data, (size_t)p_bytes);
    uploads.push_back({ upload_used, p_dst, size });
    upload_used += size;
    return true;
}

void TerrainCache::_release_slot(uint32_t p_slot, uint64_t p_frame_number)
{
    if (p_slot != TERRAIN_LOD_NONE) retired.push_back({ p_frame_number, p_slot });
}

bool TerrainCache::_alloc_slot(uint32_t p_lod, uint64_t p_frame_number, uint32_t& r_slot)
{
    for (size_t i = 0; i < retired.size();) {
        if (retired[i].frame + frame_count <= p_frame_number) {
            (retired[i].slot < TERRAIN_FINE_SLOTS ? free_fine : free_small).push_back(retired[i].slot);
            retired[i] = retired.back();
            retired.pop_back();
        } else {
            i++;
        }
    }
    const bool fine = p_lod == 0;
    std::vector<uint32_t>& pool = fine ? free_fine : free_small;
    if (pool.empty()) {
        uint32_t victim = TERRAIN_LOD_NONE;
        float farthest = -1.0f;
        for (uint32_t i = 0; i < tiles.size(); i++) {
            const Tile& tile = tiles[i];
            if (tile.slot == TERRAIN_LOD_NONE || (tile.slot < TERRAIN_FINE_SLOTS) != fine) continue;
            if (tile.desired != TERRAIN_LOD_NONE && tile.desired <= tile.lod) continue;
            if (tile.distance > farthest) {
                farthest = tile.distance;
                victim = i;
            }
        }
        if (victim == TERRAIN_LOD_NONE) return false;
        Tile& v = tiles[victim];
        _release_slot(v.slot, p_frame_number);
        v.slot = TERRAIN_LOD_NONE;
        v.lod = TERRAIN_LOD_NONE;
        tile_table[victim] = { 0u, TERRAIN_LOD_NONE };
        return false;
    }
    r_slot = pool.back();
    pool.pop_back();
    return true;
}

void TerrainCache::_receive(uint64_t p_frame_number)
{
    for (size_t i = 0; i < inflight.size();) {
        if (inflight[i]->done.load(std::memory_order_acquire)) {
            ready.push_back(inflight[i]);
            inflight[i] = inflight.back();
            inflight.pop_back();
        } else {
            i++;
        }
    }

    const auto tile_of = [&](const Request& r) -> Tile& { return tiles[r.tile]; };
    std::sort(ready.begin(), ready.end(), [&](const std::shared_ptr<Request>& a, const std::shared_ptr<Request>& b) { return tile_of(*a).distance < tile_of(*b).distance; });
    size_t kept = 0;
    for (size_t i = 0; i < ready.size(); i++) {
        std::shared_ptr<Request>& r = ready[i];
        Tile& tile = tile_of(*r);
        const bool wanted = r->ok && tile.desired != TERRAIN_LOD_NONE && (tile.lod == TERRAIN_LOD_NONE || r->lod < tile.lod);
        if (!wanted) {
            if (tile.loading == r->lod) tile.loading = TERRAIN_LOD_NONE;
            continue;
        }
        uint32_t slot = TERRAIN_LOD_NONE;
        const VkDeviceSize bytes = (VkDeviceSize)r->samples.size() * sizeof(uint16_t);
        if (upload_used + ((bytes + 3) & ~(VkDeviceSize)3) > UPLOAD_BUDGET || !_alloc_slot(r->lod, p_frame_number, slot)) {
            ready[kept++] = r;
            continue;
        }
        _stage(r->samples.data(), bytes, _slot_word(slot) * sizeof(uint32_t));
        _release_slot(tile.slot, p_frame_number);
        tile.slot = slot;
        tile.lod = r->lod;
        tile.loading = TERRAIN_LOD_NONE;
        tile_table[r->tile] = { (uint32_t)_slot_word(slot), r->lod };
    }
    ready.resize(kept);
}

void TerrainCache::_plan_streaming(const vec3& p_camera)
{
    _order.clear();
    for (uint32_t t = 0; t < tiles.size(); t++) {
        vec3 lo, hi;
        _node_box(t, TERRAIN_COARSE_LEVEL, 0, 0, lo, hi);
        Tile& tile = tiles[t];
        tile.distance = _box_distance(p_camera, lo, hi);
        tile.desired = TERRAIN_LOD_NONE;
        for (uint32_t l = 0; l < STREAMED_LODS; l++) {
            if (tile.distance < ranges[l] * PREFETCH_SCALE) {
                tile.desired = l;
                break;
            }
        }
        const uint32_t have = std::min(tile.lod, tile.loading);
        if (tile.desired != TERRAIN_LOD_NONE && (have == TERRAIN_LOD_NONE || tile.desired < have)) _order.push_back(t);
    }
    std::sort(_order.begin(), _order.end(), [&](uint32_t a, uint32_t b) { return tiles[a].distance < tiles[b].distance; });

    for (const uint32_t t : _order) {
        if (inflight.size() + ready.size() >= MAX_INFLIGHT) break;
        Tile& tile = tiles[t];
        std::shared_ptr<Request> r = std::make_shared<Request>();
        r->tile = t;
        r->lod = tile.desired;
        r->samples.resize((size_t)lterrain_lod_samples(r->lod) * lterrain_lod_samples(r->lod));
        tile.loading = r->lod;
        const std::filesystem::path path = source.path;
        const uint64_t offset = source.tiles[t].offset + source.lod_offsets[r->lod];
        inflight.push_back(r);
        job_system().run_background_owned([r, path, offset] {
            r->ok = _read_range(path, offset, (uint64_t)r->samples.size() * sizeof(uint16_t), r->samples.data());
            r->done.store(true, std::memory_order_release);
        }, nullptr, "Terrain Stream");
    }
}

void TerrainCache::_node_box(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, vec3& r_min, vec3& r_max) const
{
    const LTerrainPayloadHeader& h = source.header;
    const float size = (float)(TERRAIN_PATCH_QUADS << p_level);
    const float x0 = (float)((p_tile % h.tiles_x) * LTERRAIN_TILE_QUADS) + (float)p_i * size;
    const float z0 = (float)((p_tile / h.tiles_x) * LTERRAIN_TILE_QUADS) + (float)p_j * size;
    float lo, hi;
    source.node_range(p_tile, TERRAIN_COARSE_LEVEL - p_level, p_i, p_j, lo, hi);
    r_min = origin + vec3(x0 * h.spacing, lo, z0 * h.spacing);
    r_max = origin + vec3((x0 + size) * h.spacing, hi, (z0 + size) * h.spacing);
}

void TerrainCache::_emit(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, uint32_t p_quarter)
{
    if (patch_count >= TERRAIN_MAX_PATCHES) return;
    const LTerrainPayloadHeader& h = source.header;
    const uint32_t size = TERRAIN_PATCH_QUADS << p_level;
    patches[patch_count++] = { (p_tile % h.tiles_x) * LTERRAIN_TILE_QUADS + p_i * size, (p_tile / h.tiles_x) * LTERRAIN_TILE_QUADS + p_j * size, p_tile, p_level | (p_quarter << 4) };
}

bool TerrainCache::_select_node(const CameraUniform& p_camera, uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, bool p_root)
{
    vec3 lo, hi;
    _node_box(p_tile, p_level, p_i, p_j, lo, hi);
    const vec3 cam = vec3(p_camera.position);
    const float dist = _box_distance(cam, lo, hi);
    if (!p_root && dist >= ranges[p_level]) return false;
    if (!_box_visible(p_camera.frustum_planes, lo, hi)) return true;

    const uint32_t lod = tiles[p_tile].lod;
    const bool can_refine = p_level > 0 && lod != TERRAIN_LOD_NONE && lod <= p_level - 1;
    if (!can_refine || dist >= ranges[p_level - 1]) {
        for (uint32_t q = 0; q < 4; q++) _emit(p_tile, p_level, p_i, p_j, q);
        return true;
    }
    for (uint32_t q = 0; q < 4; q++) {
        if (!_select_node(p_camera, p_tile, p_level - 1, p_i * 2 + (q & 1), p_j * 2 + (q >> 1), false)) _emit(p_tile, p_level, p_i, p_j, q);
    }
    return true;
}

void TerrainCache::_select(const CameraUniform& p_camera)
{
    patch_count = 0;
    if (coarse_pending) return;
    for (uint32_t t = 0; t < tiles.size(); t++) _select_node(p_camera, t, TERRAIN_COARSE_LEVEL, 0, 0, true);
}

void TerrainCache::update(Guid p_terrain, const CameraUniform& p_camera, uint64_t p_frame_number, uint32_t p_slot)
{
    frame_active = false;
    uploads.clear();
    upload_used = 0;
    upload_slot = p_slot;
    patch_count = 0;
    if (!dd) return;
    if (!p_terrain.valid()) {
        if (_requested.valid() || source.valid()) {
            _deactivate();
            _requested = {};
        }
        return;
    }
    if (p_terrain != _requested) _activate(p_terrain);
    if (!source.valid()) return;

    if (coarse_pending && _stage(source.coarse.data(), (VkDeviceSize)source.coarse.size() * sizeof(uint16_t), 0)) coarse_pending = false;

    const bool replay = freeze && frozen;
    if (!replay) {
        const vec3 cam = vec3(p_camera.position);
        _receive(p_frame_number);
        _plan_streaming(cam);
        patches.resize(TERRAIN_MAX_PATCHES);
        _select(p_camera);
        frozen_camera = cam;
        frozen_count = patch_count;
        frozen = freeze;
    } else {
        patch_count = frozen_count;
    }
    if (!freeze) frozen = false;

    drivers::DeviceDriverVulkan::Buffer& pb = patch_buffers[p_slot];
    drivers::DeviceDriverVulkan::Buffer& tb = tile_buffers[p_slot];
    drivers::DeviceDriverVulkan::Buffer& ub = uniform_buffers[p_slot];
    if (patch_count) {
        dd->buffer_update(pb, patches.data(), (VkDeviceSize)patch_count * sizeof(TerrainPatch));
        dd->buffer_flush(pb, 0, (VkDeviceSize)patch_count * sizeof(TerrainPatch));
    }
    if (!tile_table.empty()) {
        dd->buffer_update(tb, tile_table.data(), (VkDeviceSize)tile_table.size() * sizeof(TerrainTileGpu));
        dd->buffer_flush(tb, 0, (VkDeviceSize)tile_table.size() * sizeof(TerrainTileGpu));
    }
    if (upload_used) dd->buffer_flush(staging[p_slot], 0, upload_used);

    const LTerrainPayloadHeader& h = source.header;
    TerrainUniform& u = _uniform;
    u = {};
    u.origin_spacing = vec4(origin, h.spacing);
    u.morph_camera = vec4(frozen_camera, 0.0f);
    for (uint32_t l = 0; l < TERRAIN_LEVELS; l++) {
        if (l == TERRAIN_COARSE_LEVEL) {
            u.morph[l] = vec4(FLT_MAX, 0.0f, 0.0f, 0.0f);
            continue;
        }
        const float prev = l > 0 ? ranges[l - 1] : 0.0f;
        const float start = prev + (ranges[l] - prev) * MORPH_START;
        u.morph[l] = vec4(start, 1.0f / std::max(ranges[l] - start, 1e-3f), 0.0f, 0.0f);
    }
    u.height_min = h.height_min;
    u.height_scale = coarse_pending ? 0.0f : source.height_scale();
    u.tiles_x = h.tiles_x;
    u.tiles_y = h.tiles_y;
    u.samples_x = h.samples_x;
    u.samples_y = h.samples_y;
    u.coarse_w = h.coarse_w;
    u.coarse_h = h.coarse_h;
    u.heights = heights.device_address;
    u.tile_table = tb.device_address;
    u.patches = pb.device_address;
    u.coarse_base = 0;
    dd->buffer_update(ub, &u, sizeof(TerrainUniform));
    dd->buffer_flush(ub, 0, sizeof(TerrainUniform));
    frame_active = true;
}

}
