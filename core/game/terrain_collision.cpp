#include <core/game/terrain_collision.h>
#include <core/base/jobs.h>
#include <algorithm>
#include <cfloat>
#include <fstream>
#include <thread>

namespace lumen {

static constexpr uint32_t CORE_SAMPLES = LTERRAIN_TILE_QUADS + 1;

struct TileBuildInfo
{
    uint32_t tile_x;
    uint32_t tile_y;
    uint32_t samples_x;
    uint32_t samples_y;
    vec3 origin;
    float spacing;
    float height_min;
    float height_scale;
};

static void _build_tile(const TileBuildInfo& p_info, const std::filesystem::path& p_path, uint64_t p_offset, PhysicsHeightField& r_field)
{
    const uint32_t row = lterrain_lod_samples(0);
    std::vector<uint16_t> raw((size_t)row * row);
    std::ifstream in(p_path, std::ios::binary);
    if (!in) return;
    in.seekg((std::streamoff)p_offset);
    in.read(reinterpret_cast<char*>(raw.data()), (std::streamsize)(raw.size() * sizeof(uint16_t)));
    if (!in) return;

    std::vector<float> heights((size_t)CORE_SAMPLES * CORE_SAMPLES);
    const uint32_t gx0 = p_info.tile_x * LTERRAIN_TILE_QUADS;
    const uint32_t gy0 = p_info.tile_y * LTERRAIN_TILE_QUADS;
    for (uint32_t y = 0; y < CORE_SAMPLES; y++) {
        for (uint32_t x = 0; x < CORE_SAMPLES; x++) {
            const bool inside = gx0 + x < p_info.samples_x && gy0 + y < p_info.samples_y;
            const uint16_t v = raw[(size_t)(y + LTERRAIN_APRON) * row + x + LTERRAIN_APRON];
            heights[(size_t)y * CORE_SAMPLES + x] = inside ? p_info.height_min + (float)v * p_info.height_scale : PhysicsWorld::HEIGHT_FIELD_HOLE;
        }
    }
    const vec3 offset = p_info.origin + vec3((float)gx0 * p_info.spacing, 0.0f, (float)gy0 * p_info.spacing);
    r_field = PhysicsWorld::build_height_field(heights.data(), CORE_SAMPLES, offset, vec3(p_info.spacing, 1.0f, p_info.spacing));
}

void TerrainCollision::_wait()
{
    for (const std::shared_ptr<Build>& b : inflight) {
        while (!b->done.load(std::memory_order_acquire)) std::this_thread::yield();
        PhysicsWorld::release_height_field(b->field);
    }
    inflight.clear();
}

void TerrainCollision::clear(PhysicsWorld& r_physics)
{
    _wait();
    for (Tile& t : tiles) {
        if (t.body.valid()) r_physics.destroy_body(t.body);
    }
    tiles.clear();
    source.close();
}

void TerrainCollision::_tile_box(uint32_t p_tile, vec2& r_min, vec2& r_max) const
{
    const LTerrainPayloadHeader& h = source.header;
    const vec3 origin = source.origin();
    const float size = (float)LTERRAIN_TILE_QUADS * h.spacing;
    r_min = vec2(origin.x, origin.z) + vec2((float)(p_tile % h.tiles_x), (float)(p_tile / h.tiles_x)) * size;
    r_max = r_min + vec2(size);
}

void TerrainCollision::_build(const TerrainSource& p_source, uint32_t p_tile, const std::filesystem::path& p_path, uint64_t p_offset, Build& r_build)
{
    const LTerrainPayloadHeader& h = p_source.header;
    const TileBuildInfo info{ p_tile % h.tiles_x, p_tile / h.tiles_x, h.samples_x, h.samples_y, p_source.origin(), h.spacing, h.height_min, p_source.height_scale() };
    _build_tile(info, p_path, p_offset, r_build.field);
}

void TerrainCollision::update(PhysicsWorld& r_physics, Guid p_terrain, const std::filesystem::path& p_path, const std::vector<vec3>& p_anchors)
{
    if (!p_terrain.valid() || p_path.empty()) {
        if (source.valid()) clear(r_physics);
        return;
    }
    if (source.guid != p_terrain) {
        clear(r_physics);
        if (source.open(p_terrain, p_path) != Error::OK) {
            source.close();
            return;
        }
        tiles.assign(source.tile_count(), Tile{});
        lowest = FLT_MAX;
        for (uint32_t t = 0; t < source.tile_count(); t++) {
            float lo, hi;
            source.node_range(t, 0, 0, 0, lo, hi);
            lowest = std::min(lowest, lo);
        }
    }
    if (!source.valid()) return;

    for (size_t i = 0; i < inflight.size();) {
        Build& b = *inflight[i];
        if (!b.done.load(std::memory_order_acquire)) {
            i++;
            continue;
        }
        Tile& t = tiles[b.tile];
        t.loading = false;
        if (t.distance < UNLOAD_RADIUS && !t.body.valid()) t.body = r_physics.create_height_field_body(b.field, FRICTION);
        else PhysicsWorld::release_height_field(b.field);
        inflight[i] = inflight.back();
        inflight.pop_back();
    }

    _order.clear();
    for (uint32_t i = 0; i < tiles.size(); i++) {
        vec2 lo, hi;
        _tile_box(i, lo, hi);
        float best = FLT_MAX;
        for (const vec3& a : p_anchors) best = std::min(best, length(glm::max(glm::max(lo - vec2(a.x, a.z), vec2(0.0f)), vec2(a.x, a.z) - hi)));
        Tile& t = tiles[i];
        t.distance = best;
        if (t.body.valid() && best > UNLOAD_RADIUS) {
            r_physics.destroy_body(t.body);
            t.body = {};
        }
        if (!t.body.valid() && !t.loading && best < LOAD_RADIUS) _order.push_back(i);
    }
    std::sort(_order.begin(), _order.end(), [&](uint32_t a, uint32_t b) { return tiles[a].distance < tiles[b].distance; });

    for (const uint32_t i : _order) {
        Tile& t = tiles[i];
        const uint64_t offset = source.tiles[i].offset + source.lod_offsets[0];
        if (t.distance <= 0.0f) {
            Build b;
            _build(source, i, source.path, offset, b);
            t.body = r_physics.create_height_field_body(b.field, FRICTION);
            continue;
        }
        if (inflight.size() >= MAX_INFLIGHT) continue;
        std::shared_ptr<Build> b = std::make_shared<Build>();
        b->tile = i;
        t.loading = true;
        const LTerrainPayloadHeader& h = source.header;
        const TileBuildInfo info{ i % h.tiles_x, i / h.tiles_x, h.samples_x, h.samples_y, source.origin(), h.spacing, h.height_min, source.height_scale() };
        const std::filesystem::path path = source.path;
        inflight.push_back(b);
        job_system().run_background_owned([b, info, path, offset] {
            _build_tile(info, path, offset, b->field);
            b->done.store(true, std::memory_order_release);
        }, nullptr, "Terrain Collision");
    }
}

}
