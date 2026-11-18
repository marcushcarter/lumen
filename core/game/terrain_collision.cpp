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

static TileBuildInfo _build_info(const TerrainCollision::Field& p_field, uint32_t p_tile)
{
    const LTerrainPayloadHeader& h = p_field.source.header;
    return { p_tile % h.tiles_x, p_tile / h.tiles_x, h.samples_x, h.samples_y, p_field.source.origin() + p_field.position, h.spacing, h.height_min, p_field.source.height_scale() };
}

bool TerrainCollision::valid() const
{
    for (const std::unique_ptr<Field>& f : fields) {
        if (f->source.valid()) return true;
    }
    return false;
}

void TerrainCollision::_clear_field(PhysicsWorld& r_physics, Field& r_field)
{
    for (const std::shared_ptr<Build>& b : r_field.inflight) {
        while (!b->done.load(std::memory_order_acquire)) std::this_thread::yield();
        PhysicsWorld::release_height_field(b->field);
    }
    r_field.inflight.clear();
    for (Tile& t : r_field.tiles) {
        if (t.body.valid()) r_physics.destroy_body(t.body);
    }
    r_field.tiles.clear();
    r_field.source.close();
}

void TerrainCollision::clear(PhysicsWorld& r_physics)
{
    for (std::unique_ptr<Field>& f : fields) _clear_field(r_physics, *f);
    fields.clear();
}

void TerrainCollision::_tile_box(const Field& p_field, uint32_t p_tile, vec2& r_min, vec2& r_max)
{
    const LTerrainPayloadHeader& h = p_field.source.header;
    const vec3 origin = p_field.source.origin() + p_field.position;
    const float size = (float)LTERRAIN_TILE_QUADS * h.spacing;
    r_min = vec2(origin.x, origin.z) + vec2((float)(p_tile % h.tiles_x), (float)(p_tile / h.tiles_x)) * size;
    r_max = r_min + vec2(size);
}

void TerrainCollision::update(PhysicsWorld& r_physics, const std::vector<TerrainInstance>& p_terrains, const std::vector<std::filesystem::path>& p_paths, const std::vector<vec3>& p_anchors)
{
    for (size_t i = 0; i < fields.size();) {
        Field& f = *fields[i];
        bool keep = false;
        for (size_t j = 0; j < p_terrains.size(); j++) {
            if (p_terrains[j].key == f.key) {
                keep = p_terrains[j].guid == f.guid && p_terrains[j].position == f.position && p_paths[j] == f.path;
                break;
            }
        }
        if (keep) {
            i++;
            continue;
        }
        _clear_field(r_physics, f);
        fields[i] = std::move(fields.back());
        fields.pop_back();
    }

    for (size_t j = 0; j < p_terrains.size(); j++) {
        const TerrainInstance& inst = p_terrains[j];
        bool known = false;
        for (const std::unique_ptr<Field>& f : fields) known |= f->key == inst.key;
        if (known) continue;
        std::unique_ptr<Field> f = std::make_unique<Field>();
        f->key = inst.key;
        f->guid = inst.guid;
        f->position = inst.position;
        f->path = p_paths[j];
        if (!f->path.empty() && f->source.open(inst.guid, f->path) == Error::OK) {
            f->tiles.assign(f->source.tile_count(), Tile{});
            f->lowest = FLT_MAX;
            for (uint32_t t = 0; t < f->source.tile_count(); t++) {
                float lo, hi;
                f->source.node_range(t, 0, 0, 0, lo, hi);
                f->lowest = std::min(f->lowest, lo);
            }
        } else {
            f->source.close();
        }
        fields.push_back(std::move(f));
    }

    lowest = FLT_MAX;
    for (std::unique_ptr<Field>& f : fields) {
        if (!f->source.valid()) continue;
        _update_field(r_physics, *f, p_anchors);
        lowest = std::min(lowest, f->lowest);
    }
}

void TerrainCollision::_update_field(PhysicsWorld& r_physics, Field& r_field, const std::vector<vec3>& p_anchors)
{
    std::vector<std::shared_ptr<Build>>& inflight = r_field.inflight;
    std::vector<Tile>& tiles = r_field.tiles;
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
        _tile_box(r_field, i, lo, hi);
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
        const uint64_t offset = r_field.source.tiles[i].offset + r_field.source.lod_offsets[0];
        const TileBuildInfo info = _build_info(r_field, i);
        if (t.distance <= 0.0f) {
            Build b;
            _build_tile(info, r_field.source.path, offset, b.field);
            t.body = r_physics.create_height_field_body(b.field, FRICTION);
            continue;
        }
        if (inflight.size() >= MAX_INFLIGHT) continue;
        std::shared_ptr<Build> b = std::make_shared<Build>();
        b->tile = i;
        t.loading = true;
        const std::filesystem::path path = r_field.source.path;
        inflight.push_back(b);
        job_system().run_background_owned([b, info, path, offset] {
            _build_tile(info, path, offset, b->field);
            b->done.store(true, std::memory_order_release);
        }, nullptr, "Terrain Collision");
    }
}

}
