#include <core/water/water_level.h>
#include <core/physics/physics_world.h>
#include <core/terrain/terrain_source.h>
#include <core/rendering/debug_draw.h>
#include <core/scene/scene.h>
#include <core/scene/components.h>
#include <core/base/jobs.h>
#include <core/base/hash.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <string>

namespace lumen {

using namespace water;

static uint64_t g_water_layout = 0;

static uint64_t _hash_float(uint64_t p_hash, float p_value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &p_value, sizeof(bits));
    return hash_combine(p_hash, bits);
}

WaterLevel::~WaterLevel()
{
    clear();
}

void WaterLevel::clear()
{
    placed = false;
    tiles.clear();
    capacity = 0;
    _surface_capacity = 0;
    free_phys.clear();
    h.clear();
    b.clear();
    foam.clear();
    u.clear();
    w.clear();
    uc.clear();
    wc.clear();
    flags.clear();
    beds.clear();
    frozen.clear();
    frozen_added.clear();
    frozen_removed.clear();
    _bed_queue.clear();
    _sources.clear();
    _source_hash = 0;
    stats = {};
    if (_page_file.is_open()) _page_file.close();
    if (!_page_path.empty()) {
        std::error_code ec;
        std::filesystem::remove(_page_path, ec);
        _page_path.clear();
    }
    _page_end = 0;
    _page_free.clear();
    tiles_w = 0;
    n = 0;
    slots = 0;
    layout = ++g_water_layout;
}

void WaterLevel::configure(float p_dx, int32_t p_tiles_w)
{
    if (!tiles.empty() && p_dx == dx && p_tiles_w == tiles_w) return;
    clear();
    dx = p_dx;
    tiles_w = p_tiles_w;
    n = tiles_w * TILE;
    slots = tiles_w + 2;
    tiles.assign((size_t)slots * slots, Tile{});
}

int32_t WaterLevel::at(int32_t p_i, int32_t p_j) const
{
    const ivec2 lt(floor_div(p_i, TILE), floor_div(p_j, TILE));
    if (!in_window(lt)) return -1;
    const Tile& t = tiles[slot_of(lt)];
    if (t.phys == NO_PHYS) return -1;
    return (int32_t)(t.phys * TILE_CELLS) + (p_j & 15) * TILE + (p_i & 15);
}

const WaterLevel::FrozenState* WaterLevel::_frozen_state(ivec2 p_global_cell, int32_t& r_index) const
{
    if (frozen.empty()) return nullptr;
    const ivec2 t(floor_div(p_global_cell.x, TILE), floor_div(p_global_cell.y, TILE));
    const auto it = frozen.find(tile_key(t));
    if (it == frozen.end() || !it->second.state) return nullptr;
    r_index = (p_global_cell.y - t.y * TILE) * TILE + (p_global_cell.x - t.x * TILE);
    return it->second.state.get();
}

bool WaterLevel::cell_state(ivec2 p_global, float& r_h, float& r_b) const
{
    if (placed) {
        const ivec2 l = p_global - origin_cell();
        const ivec2 lt(floor_div(l.x, TILE), floor_div(l.y, TILE));
        if (in_interior(lt)) {
            const int32_t c = at(l.x, l.y);
            if (c >= 0 && tiles[slot_of(lt)].active) {
                r_h = h[c];
                r_b = b[c];
                return r_b < WALL_TEST;
            }
        }
    }
    int32_t k = 0;
    if (const FrozenState* s = _frozen_state(p_global, k)) {
        r_h = s->h[k];
        r_b = s->b[k];
        return r_b < WALL_TEST;
    }
    return false;
}

bool WaterLevel::inside(float p_x, float p_z) const
{
    if (!placed) return false;
    const vec4 r = interior_rect();
    return p_x >= r.x && p_z >= r.y && p_x < r.z && p_z < r.w;
}

vec4 WaterLevel::interior_rect() const
{
    const vec2 lo = vec2(origin_cell()) * dx;
    return vec4(lo, lo + vec2((float)n * dx));
}

uint32_t WaterLevel::_alloc()
{
    if (free_phys.empty()) {
        const uint32_t grown = std::max(CAPACITY_MIN, capacity + capacity / 2);
        const size_t cells = (size_t)grown * TILE_CELLS;
        h.resize(cells, 0.0f);
        b.resize(cells, WALL);
        foam.resize(cells, 0.0f);
        u.resize(cells, 0.0f);
        w.resize(cells, 0.0f);
        uc.resize(cells, 0.0f);
        wc.resize(cells, 0.0f);
        flags.resize(cells, 0u);
        for (uint32_t p = grown; p-- > capacity;) free_phys.push_back(p);
        capacity = grown;
    }
    const uint32_t p = free_phys.back();
    free_phys.pop_back();
    return p;
}

void WaterLevel::_release(uint32_t p_phys)
{
    if (p_phys != NO_PHYS) free_phys.push_back(p_phys);
}

void WaterLevel::_init_phys(uint32_t p_phys)
{
    const size_t o = (size_t)p_phys * TILE_CELLS;
    std::fill_n(h.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(b.begin() + o, TILE_CELLS, WALL);
    std::fill_n(foam.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(u.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(w.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(uc.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(wc.begin() + o, TILE_CELLS, 0.0f);
    std::fill_n(flags.begin() + o, TILE_CELLS, 0u);
    if (gpu) gpu->init.push_back(p_phys);
}

void WaterLevel::_upload(uint32_t p_array, uint32_t p_phys, const float* p_data)
{
    if (!gpu) return;
    gpu->uploads.push_back({ p_array, p_phys, (uint32_t)gpu->blob.size() });
    gpu->blob.insert(gpu->blob.end(), p_data, p_data + TILE_CELLS);
}

void WaterLevel::_upload_mirror(uint32_t p_phys, bool p_all)
{
    const size_t o = (size_t)p_phys * TILE_CELLS;
    _upload(B, p_phys, b.data() + o);
    _upload(FLAGS, p_phys, reinterpret_cast<const float*>(flags.data() + o));
    if (!p_all) return;
    _upload(H, p_phys, h.data() + o);
    _upload(FOAM, p_phys, foam.data() + o);
    _upload(U, p_phys, u.data() + o);
    _upload(W, p_phys, w.data() + o);
}

void WaterLevel::_event(uint32_t p_array, int32_t p_cell, float p_lerp, float p_target, float p_add)
{
    if (!gpu || p_cell < 0) return;
    gpu->events.push_back({ p_array, (uint32_t)p_cell, p_lerp, p_target, p_add });
}

void WaterLevel::_freeze(const Tile& r_tile)
{
    Frozen& f = frozen[tile_key(r_tile.global)];
    if (!f.state) f.state = std::make_unique<FrozenState>();
    const size_t o = (size_t)r_tile.phys * TILE_CELLS;
    std::memcpy(f.state->h, h.data() + o, sizeof(f.state->h));
    std::memcpy(f.state->b, b.data() + o, sizeof(f.state->b));
    std::memcpy(f.state->foam, foam.data() + o, sizeof(f.state->foam));
    std::memcpy(f.state->u, u.data() + o, sizeof(f.state->u));
    std::memcpy(f.state->w, w.data() + o, sizeof(f.state->w));
    std::memcpy(f.state->flags, flags.data() + o, sizeof(f.state->flags));
    f.bed = r_tile.bed == Bed::MISSING ? Bed::MISSING : Bed::READY;
    f.has_surface = false;
    frozen_added.push_back(tile_key(r_tile.global));
}

void WaterLevel::_evict(Tile& r_tile, bool p_interior)
{
    if (r_tile.phys != NO_PHYS) {
        if (p_interior && r_tile.active) {
            if (r_tile.max_h > EPS || r_tile.source) _freeze(r_tile);
            BedCache& bed = beds[tile_key(r_tile.global)];
            const size_t o = (size_t)r_tile.phys * TILE_CELLS;
            std::memcpy(bed.b, b.data() + o, sizeof(bed.b));
            std::memcpy(bed.flags, flags.data() + o, sizeof(bed.flags));
            bed.missing = r_tile.bed == Bed::MISSING;
        }
        _release(r_tile.phys);
    }
    r_tile = Tile{};
    _alloc_epoch++;
}

bool WaterLevel::_load_page(Frozen& r_frozen)
{
    if (r_frozen.state) return true;
    if (r_frozen.page_offset == UINT64_MAX || !_page_file.is_open()) return false;
    r_frozen.state = std::make_unique<FrozenState>();
    _page_file.seekg((std::streamoff)r_frozen.page_offset);
    _page_file.read(reinterpret_cast<char*>(r_frozen.state.get()), sizeof(FrozenState));
    _page_file.clear();
    _page_free.push_back(r_frozen.page_offset);
    r_frozen.page_offset = UINT64_MAX;
    return true;
}

void WaterLevel::_thaw(Tile& r_tile, ivec2 p_local)
{
    (void)p_local;
    const auto it = frozen.find(tile_key(r_tile.global));
    if (it == frozen.end()) return;
    Frozen& f = it->second;
    if (!_load_page(f)) return;
    r_tile.phys = _alloc();
    _init_phys(r_tile.phys);
    const size_t o = (size_t)r_tile.phys * TILE_CELLS;
    std::memcpy(h.data() + o, f.state->h, sizeof(f.state->h));
    std::memcpy(b.data() + o, f.state->b, sizeof(f.state->b));
    std::memcpy(foam.data() + o, f.state->foam, sizeof(f.state->foam));
    std::memcpy(u.data() + o, f.state->u, sizeof(f.state->u));
    std::memcpy(w.data() + o, f.state->w, sizeof(f.state->w));
    std::memcpy(flags.data() + o, f.state->flags, sizeof(f.state->flags));
    _upload_mirror(r_tile.phys, true);
    r_tile.bed = f.bed;
    r_tile.active = true;
    r_tile.fresh_frames = FRESH_FRAMES;
    r_tile.max_h = 0.0f;
    r_tile.surface_dirty = true;
    r_tile.surface_built = false;
    for (int32_t k = 0; k < TILE_CELLS; k++) r_tile.max_h = std::max(r_tile.max_h, f.state->h[k]);
    frozen_removed.push_back({ it->first, f.pool });
    frozen.erase(it);
    _alloc_epoch++;
}

void WaterLevel::_enter(Tile& r_tile, ivec2 p_local)
{
    r_tile.ring = !in_interior(p_local);
    if (!r_tile.ring) {
        _thaw(r_tile, p_local);
        return;
    }
    r_tile.phys = _alloc();
    _init_phys(r_tile.phys);
    const auto it = frozen.find(tile_key(r_tile.global));
    if (it == frozen.end() || !_load_page(it->second)) {
        _upload_mirror(r_tile.phys, false);
        return;
    }
    const FrozenState& s = *it->second.state;
    const size_t o = (size_t)r_tile.phys * TILE_CELLS;
    std::memcpy(h.data() + o, s.h, sizeof(s.h));
    std::memcpy(b.data() + o, s.b, sizeof(s.b));
    std::memcpy(foam.data() + o, s.foam, sizeof(s.foam));
    std::memcpy(u.data() + o, s.u, sizeof(s.u));
    std::memcpy(w.data() + o, s.w, sizeof(s.w));
    for (int32_t k = 0; k < TILE_CELLS; k++) flags[o + k] = s.flags[k] | CELL_KNOWN;
    _upload_mirror(r_tile.phys, true);
}

void WaterLevel::_recenter(ivec2 p_origin_tile)
{
    struct Change { int32_t slot; ivec2 local; ivec2 global; };
    std::vector<Change> changes;
    for (int32_t y = -1; y <= tiles_w; y++) {
        for (int32_t x = -1; x <= tiles_w; x++) {
            const ivec2 lt(x, y);
            const ivec2 g = p_origin_tile + lt;
            const int32_t s = water::wrap(g.y, slots) * slots + water::wrap(g.x, slots);
            const Tile& t = tiles[s];
            const bool ring = !in_interior(lt);
            if (placed && t.global == g && t.ring == ring) continue;
            changes.push_back({ s, lt, g });
        }
    }
    for (const Change& c : changes) {
        Tile& t = tiles[c.slot];
        if (t.global.x != INT32_MIN) _evict(t, !t.ring);
    }
    origin_tile = p_origin_tile;
    placed = true;
    for (uint64_t key : frozen_added) {
        auto it = frozen.find(key);
        if (it == frozen.end()) continue;
        Frozen& f = it->second;
        f.has_surface = surface_patch(key_tile(key) * TILE, f.surface, f.mask, f.ymin, f.ymax);
    }
    for (const Change& c : changes) {
        Tile& t = tiles[c.slot];
        t = Tile{};
        t.global = c.global;
        _enter(t, c.local);
    }
    _bed_queue.clear();
    _source_hash = 0;
    _alloc_epoch++;
    const float keep = BED_CACHE_KEEP * (float)tiles_w * 0.5f;
    const ivec2 center = origin_tile + ivec2(tiles_w / 2);
    for (auto it = beds.begin(); it != beds.end();) {
        const vec2 d = vec2(key_tile(it->first) - center);
        if (dot(d, d) > keep * keep) it = beds.erase(it);
        else ++it;
    }
}

void WaterLevel::_activate(Tile& r_tile, ivec2 p_local, const BedCache& p_bed)
{
    (void)p_local;
    r_tile.phys = _alloc();
    _init_phys(r_tile.phys);
    const size_t o = (size_t)r_tile.phys * TILE_CELLS;
    std::memcpy(b.data() + o, p_bed.b, sizeof(p_bed.b));
    std::memcpy(flags.data() + o, p_bed.flags, sizeof(p_bed.flags));
    _upload_mirror(r_tile.phys, false);
    r_tile.bed = p_bed.missing ? Bed::MISSING : Bed::READY;
    r_tile.active = true;
    r_tile.awake = true;
    r_tile.retry = 0;
    r_tile.fresh_frames = FRESH_FRAMES;
    r_tile.max_h = 0.0f;
    r_tile.surface_dirty = true;
    r_tile.surface_built = false;
    _alloc_epoch++;
}

void WaterLevel::_deactivate(Tile& r_tile)
{
    if (r_tile.phys != NO_PHYS) {
        BedCache& bed = beds[tile_key(r_tile.global)];
        const size_t o = (size_t)r_tile.phys * TILE_CELLS;
        std::memcpy(bed.b, b.data() + o, sizeof(bed.b));
        std::memcpy(bed.flags, flags.data() + o, sizeof(bed.flags));
        bed.missing = r_tile.bed == Bed::MISSING;
        _release(r_tile.phys);
    }
    r_tile.phys = NO_PHYS;
    r_tile.active = false;
    r_tile.awake = false;
    r_tile.max_h = 0.0f;
    r_tile.bed = Bed::NONE;
    _alloc_epoch++;
}

void WaterLevel::prepare(const Scene& p_world, const vec3& p_focus, const TerrainSource* p_terrain, const PhysicsWorld& p_physics)
{
    settings = p_world.water;
    gravity = std::max(p_world.physics.gravity, 0.01f);
    focus = p_focus;
    const float tile_size = dx * (float)TILE;
    const ivec2 focus_tile((int32_t)std::floor(p_focus.x / tile_size), (int32_t)std::floor(p_focus.z / tile_size));
    const ivec2 want = focus_tile - ivec2(tiles_w / 2);
    const ivec2 offset = abs(want - origin_tile);
    if (!placed || std::max(offset.x, offset.y) >= RECENTER_TILES) _recenter(want);
    _build_beds(p_terrain, p_physics);
    _gather_sources(p_world);
    _classify();
}

float WaterLevel::_probe_top(ivec2 p_global, const TerrainSource* p_terrain) const
{
    const vec2 lo = vec2(p_global * TILE) * dx;
    const vec2 hi = lo + vec2(dx * (float)TILE);
    float top = -FLT_MAX;
    if (p_terrain && p_terrain->valid()) {
        const LTerrainPayloadHeader& th = p_terrain->header;
        const vec3 origin = p_terrain->origin();
        const float size = (float)LTERRAIN_TILE_QUADS * th.spacing;
        const uint32_t level = LTERRAIN_LOD_COUNT - 1;
        const int32_t nodes = 1 << level;
        const float node = size / (float)nodes;
        const int32_t ax = std::max((int32_t)std::floor((lo.x - origin.x) / size), 0);
        const int32_t bx = std::min((int32_t)std::floor((hi.x - origin.x) / size), (int32_t)th.tiles_x - 1);
        const int32_t ay = std::max((int32_t)std::floor((lo.y - origin.z) / size), 0);
        const int32_t by = std::min((int32_t)std::floor((hi.y - origin.z) / size), (int32_t)th.tiles_y - 1);
        for (int32_t gy = ay; gy <= by; gy++) {
            for (int32_t gx = ax; gx <= bx; gx++) {
                const vec2 base = vec2(origin.x, origin.z) + vec2((float)gx, (float)gy) * size;
                const int32_t ni0 = std::clamp((int32_t)std::floor((lo.x - base.x) / node), 0, nodes - 1);
                const int32_t ni1 = std::clamp((int32_t)std::floor((hi.x - base.x) / node), 0, nodes - 1);
                const int32_t nj0 = std::clamp((int32_t)std::floor((lo.y - base.y) / node), 0, nodes - 1);
                const int32_t nj1 = std::clamp((int32_t)std::floor((hi.y - base.y) / node), 0, nodes - 1);
                for (int32_t nj = nj0; nj <= nj1; nj++) {
                    for (int32_t ni = ni0; ni <= ni1; ni++) {
                        float mn = 0.0f, mx = 0.0f;
                        p_terrain->node_range(p_terrain->tile_index((uint32_t)gx, (uint32_t)gy), level, (uint32_t)ni, (uint32_t)nj, mn, mx);
                        top = std::max(top, mx);
                    }
                }
            }
        }
    }
    if (top == -FLT_MAX) top = focus.y;
    return top + settings.bed_probe;
}

void WaterLevel::_build_beds(const TerrainSource* p_terrain, const PhysicsWorld& p_physics)
{
    std::vector<ivec2> work;
    for (const ivec2 g : _bed_queue) {
        if (work.size() >= BED_BUDGET) break;
        const ivec2 lt = g - origin_tile;
        if (!in_interior(lt) || tiles[slot_of(lt)].global != g) continue;
        work.push_back(g);
    }
    _bed_queue.clear();
    if (work.empty()) return;

    const bool has_terrain = heights && heights->valid();
    const vec4 terrain_rect = has_terrain ? heights->rect() : vec4(0.0f);
    const float tile_size = dx * (float)TILE;
    if (has_terrain) {
        for (const ivec2 g : work) heights->prefetch(vec2(g) * tile_size - vec2(dx), vec2(g + ivec2(1)) * tile_size + vec2(dx));
    }
    std::vector<float> tops(work.size());
    for (size_t q = 0; q < work.size(); q++) tops[q] = _probe_top(work[q], p_terrain);
    std::vector<BedCache> built(work.size());
    PhysicsQuery query;
    query.layers = physics_layer_bit(PhysicsLayer::STATIC);
    job_system().parallel_for((uint32_t)work.size(), 1, [&](uint32_t q) {
        BedCache& bed = built[q];
        bed.missing = false;
        const ivec2 base = work[q] * TILE;
        const float top = tops[q];
        float th[TILE_CELLS];
        bool thv[TILE_CELLS];
        float tmin = FLT_MAX;
        for (int32_t k = 0; k < TILE_CELLS; k++) {
            const float x = ((float)(base.x + k % TILE) + 0.5f) * dx;
            const float z = ((float)(base.y + k / TILE) + 0.5f) * dx;
            thv[k] = has_terrain && heights->height(x, z, th[k]);
            if (thv[k]) tmin = std::min(tmin, th[k]);
        }
        const vec2 lo = vec2(base) * dx;
        const vec2 hi = lo + vec2(tile_size);
        const float bottom = tmin < FLT_MAX ? tmin - BED_MESH_MARGIN : top - BED_RANGE - settings.bed_probe;
        const bool mesh = !has_terrain || p_physics.overlaps_static_object(vec3(lo.x, bottom, lo.y), vec3(hi.x, top, hi.y));
        float lowest = FLT_MAX;
        for (int32_t k = 0; k < TILE_CELLS; k++) {
            const vec3 from(((float)(base.x + k % TILE) + 0.5f) * dx, top, ((float)(base.y + k / TILE) + 0.5f) * dx);
            bed.flags[k] = 0u;
            if (mesh) {
                PhysicsHit hit;
                if (p_physics.raycast(from, vec3(0.0f, -1.0f, 0.0f), BED_RANGE + settings.bed_probe, hit, query)) {
                    bed.b[k] = thv[k] ? std::max(hit.position.y, th[k]) : hit.position.y;
                    lowest = std::min(lowest, bed.b[k]);
                    continue;
                }
            }
            if (thv[k]) {
                bed.b[k] = th[k];
                lowest = std::min(lowest, th[k]);
                continue;
            }
            const bool void_ok = !has_terrain || from.x < terrain_rect.x || from.z < terrain_rect.y || from.x > terrain_rect.z || from.z > terrain_rect.w;
            if (void_ok) {
                bed.flags[k] = CELL_DRAIN;
                bed.b[k] = -FLT_MAX;
            } else {
                bed.b[k] = WALL;
                bed.missing = true;
            }
        }
        const float floor_y = (lowest == FLT_MAX ? top - settings.bed_probe : lowest) - DRAIN_DROP;
        for (int32_t k = 0; k < TILE_CELLS; k++) {
            if (bed.flags[k] & CELL_DRAIN) bed.b[k] = floor_y;
        }
    }, JobPriority::HIGH, "Water Bed");

    for (size_t q = 0; q < work.size(); q++) {
        const ivec2 g = work[q];
        beds[tile_key(g)] = built[q];
        const ivec2 lt = g - origin_tile;
        Tile& t = tiles[slot_of(lt)];
        t.rebuild = false;
        t.retry = 0;
        if (t.active && t.phys != NO_PHYS) {
            const size_t o = (size_t)t.phys * TILE_CELLS;
            std::memcpy(b.data() + o, built[q].b, sizeof(built[q].b));
            std::memcpy(flags.data() + o, built[q].flags, sizeof(built[q].flags));
            for (int32_t k = 0; k < TILE_CELLS; k++) if (b[o + k] >= WALL_TEST) h[o + k] = 0.0f;
            _upload_mirror(t.phys, false);
            t.surface_dirty = true;
            t.bed = built[q].missing ? Bed::MISSING : Bed::READY;
            _alloc_epoch++;
        } else {
            t.bed = Bed::NONE;
            _activate(t, lt, built[q]);
        }
    }
}

void WaterLevel::static_changed(const vec3& p_min, const vec3& p_max)
{
    if (!placed) return;
    const float tile_size = dx * (float)TILE;
    const ivec2 lo = ivec2((int32_t)std::floor(p_min.x / tile_size), (int32_t)std::floor(p_min.z / tile_size));
    const ivec2 hi = ivec2((int32_t)std::floor(p_max.x / tile_size), (int32_t)std::floor(p_max.z / tile_size));
    for (int32_t y = lo.y; y <= hi.y; y++) {
        for (int32_t x = lo.x; x <= hi.x; x++) {
            beds.erase(tile_key(ivec2(x, y)));
            const ivec2 lt = ivec2(x, y) - origin_tile;
            if (!in_interior(lt)) continue;
            Tile& t = tiles[slot_of(lt)];
            if (t.active) t.rebuild = true;
        }
    }
}

void WaterLevel::_gather_sources(const Scene& p_world)
{
    uint64_t hash = hash_combine(hash_combine(tile_key(origin_tile), _alloc_epoch), (uint64_t)n);
    p_world.view<TransformComponent, WaterSourceComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&, const WaterSourceComponent& p_source) {
        const mat4 xf = p_world.world_matrix(p_entity);
        for (int32_t c = 0; c < 4; c++) for (int32_t r = 0; r < 4; r++) hash = _hash_float(hash, xf[c][r]);
        hash = hash_combine(hash, (uint64_t)p_source.type);
        hash = _hash_float(_hash_float(_hash_float(_hash_float(hash, p_source.half_extents.x), p_source.half_extents.y), p_source.half_extents.z), p_source.flow);
    });
    if (hash == _source_hash) return;
    _source_hash = hash;
    _sources.clear();
    for (Tile& t : tiles) {
        t.source = false;
        t.spring = false;
    }
    const ivec2 oc = origin_cell();
    p_world.view<TransformComponent, WaterSourceComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&, const WaterSourceComponent& p_source) {
        const mat4 xf = p_world.world_matrix(p_entity);
        const mat4 inv = inverse(xf);
        const vec3 he = p_source.half_extents;
        vec3 lo(FLT_MAX), hi(-FLT_MAX);
        for (uint32_t k = 0; k < 8; k++) {
            const vec3 corner((k & 1) ? he.x : -he.x, (k & 2) ? he.y : -he.y, (k & 4) ? he.z : -he.z);
            const vec3 p = vec3(xf * vec4(corner, 1.0f));
            lo = min(lo, p);
            hi = max(hi, p);
        }
        const int32_t i0 = std::max((int32_t)std::ceil(lo.x / dx - 0.5f) - oc.x, 0);
        const int32_t i1 = std::min((int32_t)std::floor(hi.x / dx - 0.5f) - oc.x, n - 1);
        const int32_t j0 = std::max((int32_t)std::ceil(lo.z / dx - 0.5f) - oc.y, 0);
        const int32_t j1 = std::min((int32_t)std::floor(hi.z / dx - 0.5f) - oc.y, n - 1);
        if (i0 > i1 || j0 > j1) return;
        const bool level = p_source.type == WaterSourceType::LEVEL;
        for (int32_t ty = j0 / TILE; ty <= j1 / TILE; ty++) {
            for (int32_t tx = i0 / TILE; tx <= i1 / TILE; tx++) {
                Tile& t = tiles[slot_of(ivec2(tx, ty))];
                t.source = true;
                if (!level) t.spring = true;
            }
        }
        const size_t first = _sources.size();
        for (int32_t j = j0; j <= j1; j++) {
            for (int32_t i = i0; i <= i1; i++) {
                const int32_t c = at(i, j);
                if (c < 0 || b[c] >= WALL_TEST || (flags[c] & CELL_DRAIN)) continue;
                const vec3 l = vec3(inv * vec4(((float)(oc.x + i) + 0.5f) * dx, b[c], ((float)(oc.y + j) + 0.5f) * dx, 1.0f));
                if (std::abs(l.x) > he.x || std::abs(l.z) > he.z || l.y < -he.y || l.y > he.y) continue;
                const float top = level ? vec3(xf * vec4(l.x, he.y, l.z, 1.0f)).y : 0.0f;
                _sources.push_back({ (uint32_t)c, top, level });
            }
        }
        if (level || _sources.size() == first) return;
        const float rate = p_source.flow / ((float)(_sources.size() - first) * dx * dx);
        for (size_t k = first; k < _sources.size(); k++) _sources[k].value = rate;
    });
}

void WaterLevel::_classify()
{
    const auto wet = [&](ivec2 p_local) {
        if (!in_interior(p_local)) return false;
        const Tile& t = tiles[slot_of(p_local)];
        return t.active && t.max_h > EPS;
    };
    std::vector<ivec2> later;
    for (int32_t y = 0; y < tiles_w; y++) {
        for (int32_t x = 0; x < tiles_w; x++) {
            const ivec2 lt(x, y);
            Tile& t = tiles[slot_of(lt)];
            bool wanted = t.source;
            for (int32_t oy = -1; oy <= 1 && !wanted; oy++) for (int32_t ox = -1; ox <= 1 && !wanted; ox++) wanted = wet(lt + ivec2(ox, oy));
            if (wanted) {
                if (!t.active) {
                    const auto it = beds.find(tile_key(t.global));
                    if (it != beds.end()) _activate(t, lt, it->second);
                    else if (t.bed == Bed::NONE || t.bed == Bed::QUEUED) {
                        t.bed = Bed::QUEUED;
                        _bed_queue.push_back(t.global);
                    }
                } else if (t.rebuild || (t.bed == Bed::MISSING && ++t.retry >= BED_RETRY_FRAMES)) {
                    later.push_back(t.global);
                }
            } else if (t.active) {
                _deactivate(t);
            }
        }
    }
    _bed_queue.insert(_bed_queue.end(), later.begin(), later.end());
    stats = {};
    stats.tiles = (uint32_t)(tiles_w * tiles_w);
    for (int32_t y = 0; y < tiles_w; y++) {
        for (int32_t x = 0; x < tiles_w; x++) {
            Tile& t = tiles[slot_of(ivec2(x, y))];
            t.awake = t.active;
            stats.active += t.active ? 1u : 0u;
        }
    }
    stats.awake = stats.active;
    stats.frozen = (uint32_t)frozen.size();
    for (const auto& [k, f] : frozen) stats.paged += f.state ? 0u : 1u;
}

void WaterLevel::consume(const WaterGpuResult& p_result)
{
    if (p_result.layout != layout || !placed) return;
    const ivec2 lt = p_result.tile - origin_tile;
    if (!in_interior(lt)) return;
    Tile& t = tiles[slot_of(lt)];
    if (t.global != p_result.tile || t.phys == NO_PHYS || !t.active) return;
    const size_t o = (size_t)t.phys * TILE_CELLS;
    std::memcpy(h.data() + o, p_result.data[0], sizeof(p_result.data[0]));
    std::memcpy(foam.data() + o, p_result.data[1], sizeof(p_result.data[1]));
    std::memcpy(u.data() + o, p_result.data[2], sizeof(p_result.data[2]));
    std::memcpy(w.data() + o, p_result.data[3], sizeof(p_result.data[3]));
    std::memcpy(uc.data() + o, p_result.data[4], sizeof(p_result.data[4]));
    std::memcpy(wc.data() + o, p_result.data[5], sizeof(p_result.data[5]));
}

void WaterLevel::consume_stats(uint64_t p_layout, const std::vector<float>& p_stats)
{
    if (p_layout != layout || !placed || p_stats.empty()) return;
    for (int32_t y = 0; y < tiles_w; y++) {
        for (int32_t x = 0; x < tiles_w; x++) {
            Tile& t = tiles[slot_of(ivec2(x, y))];
            if (!t.active || t.ring || t.phys == NO_PHYS || (size_t)t.phys >= p_stats.size()) continue;
            if (t.fresh_frames > 0) {
                t.fresh_frames--;
                continue;
            }
            t.max_h = p_stats[t.phys];
        }
    }
}

void WaterLevel::publish()
{
    if (!gpu) return;
    gpu->layout = layout;
    gpu->tiles_w = tiles_w;
    gpu->slots = slots;
    gpu->origin_tile = origin_tile;
    gpu->capacity = capacity;
    WaterGpuParams& p = gpu->params;
    p.dx = dx;
    p.gravity = gravity;
    p.manning = settings.manning;
    p.vorticity = settings.vorticity;
    p.vortex_min_depth = settings.vortex_min_depth;
    p.cfl_limit = settings.cfl_limit;
    p.flux_depth_cap = settings.flux_depth_cap;
    p.foam_decay = settings.foam_decay;
    p.foam_gain = settings.foam_gain;
    p.substeps = std::clamp(settings.substeps, 1u, 8u);
    gpu->page.assign((size_t)slots * slots, PAGE_EMPTY);
    gpu->awake.clear();
    gpu->surface.clear();
    if (!placed) return;
    const bool all = _surface_capacity != capacity;
    _surface_capacity = capacity;
    for (int32_t y = -1; y <= tiles_w; y++) {
        for (int32_t x = -1; x <= tiles_w; x++) {
            const ivec2 lt(x, y);
            const int32_t s = slot_of(lt);
            Tile& t = tiles[s];
            if (t.phys == NO_PHYS) continue;
            uint32_t e = t.phys;
            const bool run = t.active && !t.ring;
            if (t.active) e |= PAGE_ACTIVE;
            if (run) e |= PAGE_AWAKE;
            gpu->page[s] = e;
            if (!run) continue;
            gpu->awake.push_back(pack_tile(lt));
            gpu->surface.push_back(pack_tile(lt) | (all || t.surface_dirty ? SURFACE_FRESH : 0u));
            t.surface_dirty = false;
            t.surface_built = true;
        }
    }
    gpu->sources.clear();
    for (const SourceCell& c : _sources) gpu->sources.push_back({ H, c.cell, c.value, c.level ? 1.0f : 0.0f, 0.0f });
}

void WaterLevel::page_out(float p_distance)
{
    if (frozen.empty()) return;
    if (!_page_file.is_open()) {
        static uint32_t g_page_id = 0;
        _page_path = std::filesystem::temp_directory_path() / ("lumen_water_" + std::to_string(++g_page_id) + "_" + std::to_string((uintptr_t)this) + ".bin");
        _page_file.open(_page_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!_page_file.is_open()) return;
    }
    const vec2 f(focus.x, focus.z);
    const float tile_size = dx * (float)TILE;
    for (auto& [key, fr] : frozen) {
        if (!fr.state) continue;
        const vec2 c = (vec2(key_tile(key)) + 0.5f) * tile_size;
        if (length(c - f) < p_distance) continue;
        uint64_t offset = _page_end;
        if (!_page_free.empty()) {
            offset = _page_free.back();
            _page_free.pop_back();
        } else {
            _page_end += sizeof(FrozenState);
        }
        _page_file.seekp((std::streamoff)offset);
        _page_file.write(reinterpret_cast<const char*>(fr.state.get()), sizeof(FrozenState));
        if (!_page_file) {
            _page_file.clear();
            continue;
        }
        fr.page_offset = offset;
        fr.state.reset();
    }
    _page_file.flush();
}

vec4 WaterLevel::surface_sample(ivec2 p_global) const
{
    float hh = 0.0f, bb = 0.0f;
    if (!cell_state(p_global, hh, bb)) return vec4(0.0f, SAMPLE_INVALID, 0.0f, 0.0f);
    if (hh > WET) {
        vec2 flow(0.0f);
        float fm = 0.0f;
        const ivec2 l = p_global - origin_cell();
        const int32_t c = placed ? at(l.x, l.y) : -1;
        if (c >= 0) {
            flow = vec2(uc[c], wc[c]);
            fm = foam[c];
        } else {
            int32_t k = 0;
            if (const FrozenState* s = _frozen_state(p_global, k)) {
                flow = vec2(s->u[k], s->w[k]);
                fm = s->foam[k];
            }
        }
        const uint32_t packed_flow = glm::packHalf2x16(flow);
        const uint32_t packed_foam = glm::packHalf2x16(vec2(std::min(fm, 1.0f), 0.0f));
        float flow_bits = 0.0f, foam_bits = 0.0f;
        std::memcpy(&flow_bits, &packed_flow, sizeof(flow_bits));
        std::memcpy(&foam_bits, &packed_foam, sizeof(foam_bits));
        return vec4(hh + bb, SAMPLE_WET + hh, flow_bits, foam_bits);
    }
    float sum = 0.0f, weight = 0.0f, ext_sum = 0.0f, ext_weight = 0.0f;
    for (int32_t oy = -EXT_REACH; oy <= EXT_REACH; oy++) {
        for (int32_t ox = -EXT_REACH; ox <= EXT_REACH; ox++) {
            if (ox == 0 && oy == 0) continue;
            float nh = 0.0f, nb = 0.0f;
            if (!cell_state(p_global + ivec2(ox, oy), nh, nb) || nh <= WET) continue;
            const float wt = 1.0f / (float)(ox * ox + oy * oy);
            ext_sum += wt * (nh + nb);
            ext_weight += wt;
            if (std::abs(ox) <= SHORE_REACH && std::abs(oy) <= SHORE_REACH) {
                sum += wt * (nh + nb);
                weight += wt;
            }
        }
    }
    if (weight > 0.0f) {
        const float avg = sum / weight;
        if (bb >= avg - SHORE_TOLERANCE) return vec4(avg, SAMPLE_SHORE, 0.0f, 0.0f);
    }
    float y = bb - HIDE_DEPTH;
    if (ext_weight > 0.0f) y = std::min(y, ext_sum / ext_weight - HIDE_DEPTH);
    return vec4(y, SAMPLE_HIDDEN, 0.0f, 0.0f);
}

bool WaterLevel::surface_patch(ivec2 p_global, vec4* r_samples, uint32_t* r_mask, float& r_ymin, float& r_ymax) const
{
    bool any = false;
    for (int32_t y = 0; y < SURFACE_ROW; y++) {
        for (int32_t x = 0; x < SURFACE_ROW; x++) {
            const vec4 s = surface_sample(p_global + ivec2(x, y));
            r_samples[y * SURFACE_ROW + x] = s;
            any = any || s.y >= SAMPLE_WET;
        }
    }
    for (int32_t k = 0; k < SURFACE_SAMPLES; k++) {
        if (r_samples[k].y != SAMPLE_INVALID) continue;
        const int32_t x = k % SURFACE_ROW, y = k / SURFACE_ROW;
        float sum = 0.0f;
        int32_t count = 0;
        for (int32_t oy = -1; oy <= 1; oy++) {
            for (int32_t ox = -1; ox <= 1; ox++) {
                const int32_t nx = x + ox, ny = y + oy;
                if (nx < 0 || ny < 0 || nx >= SURFACE_ROW || ny >= SURFACE_ROW) continue;
                const vec4 s = r_samples[ny * SURFACE_ROW + nx];
                if (s.y == SAMPLE_INVALID) continue;
                sum += s.x;
                count++;
            }
        }
        r_samples[k].x = count ? sum / (float)count - HIDE_DEPTH : 0.0f;
    }
    std::fill_n(r_mask, SURFACE_MASK_WORDS, 0u);
    r_ymin = FLT_MAX;
    r_ymax = -FLT_MAX;
    for (int32_t cy = 0; cy < TILE; cy++) {
        for (int32_t cx = 0; cx < TILE; cx++) {
            bool valid = true, show = false;
            for (int32_t k = 0; k < 4; k++) {
                const vec4 s = r_samples[(cy + (k >> 1)) * SURFACE_ROW + cx + (k & 1)];
                valid = valid && s.y != SAMPLE_INVALID;
                show = show || s.y >= SAMPLE_SHORE;
            }
            if (!valid || !show) continue;
            const int32_t c = cy * TILE + cx;
            r_mask[c >> 5] |= 1u << (c & 31);
            for (int32_t k = 0; k < 4; k++) {
                const float y = r_samples[(cy + (k >> 1)) * SURFACE_ROW + cx + (k & 1)].x;
                r_ymin = std::min(r_ymin, y);
                r_ymax = std::max(r_ymax, y);
            }
        }
    }
    return any && r_ymin <= r_ymax;
}

WaterSample WaterLevel::query(float p_x, float p_z) const
{
    WaterSample q;
    const float gx = p_x / dx - 0.5f;
    const float gz = p_z / dx - 0.5f;
    const ivec2 c0((int32_t)std::floor(gx), (int32_t)std::floor(gz));
    const float tx = gx - (float)c0.x;
    const float tz = gz - (float)c0.y;
    float sum = 0.0f, hh = 0.0f, bb = 0.0f;
    for (int32_t k = 0; k < 4; k++) {
        float ch = 0.0f, cb = 0.0f;
        if (!cell_state(c0 + ivec2(k & 1, k >> 1), ch, cb)) continue;
        const float wt = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? tz : 1.0f - tz);
        sum += wt;
        hh += wt * ch;
        bb += wt * cb;
    }
    if (sum <= 1.0e-6f) return q;
    q.h = hh / sum;
    q.eta = q.h + bb / sum;
    const ivec2 g((int32_t)std::floor(p_x / dx), (int32_t)std::floor(p_z / dx));
    const ivec2 l = g - origin_cell();
    const int32_t c = placed ? at(l.x, l.y) : -1;
    if (c >= 0) {
        q.u = uc[c];
        q.w = wc[c];
    } else {
        int32_t k = 0;
        if (const FrozenState* s = _frozen_state(g, k)) {
            q.u = s->u[k];
            q.w = s->w[k];
        }
    }
    q.valid = true;
    return q;
}

void WaterLevel::push(const vec3& p_point, float p_submerged, float p_volume, const vec3& p_velocity, const vec3& p_relative)
{
    if (!placed) return;
    const ivec2 g = ivec2((int32_t)std::floor(p_point.x / dx), (int32_t)std::floor(p_point.z / dx)) - origin_cell();
    if (g.x < 0 || g.y < 0 || g.x >= n || g.y >= n) return;
    const ivec2 lt(g.x / TILE, g.y / TILE);
    Tile& t = tiles[slot_of(lt)];
    const int32_t c = at(g.x, g.y);
    if (c < 0 || !t.active || b[c] >= WALL_TEST) return;
    const float a = std::clamp(p_submerged * p_volume / (dx * dx * std::max(h[c], PUSH_MIN_DEPTH)) * PUSH_GAIN, 0.0f, PUSH_MAX);
    const int32_t cr = at(g.x + 1, g.y);
    const int32_t ct = at(g.x, g.y + 1);
    _event(U, c, a, p_velocity.x, 0.0f);
    _event(U, cr, a, p_velocity.x, 0.0f);
    _event(W, c, a, p_velocity.z, 0.0f);
    _event(W, ct, a, p_velocity.z, 0.0f);
    u[c] += a * (p_velocity.x - u[c]);
    w[c] += a * (p_velocity.z - w[c]);
    const float rel = length(p_relative);
    if (rel > PUSH_FOAM_SPEED && p_submerged < 1.0f) {
        const float add = PUSH_FOAM * p_submerged * rel;
        _event(FOAM, c, 0.0f, 0.0f, add);
        foam[c] = std::min(foam[c] + add, FOAM_MAX);
    }
}

void WaterLevel::splash(const vec3& p_point, float p_strength, float p_radius)
{
    if (!placed || p_radius <= 0.0f) return;
    const ivec2 oc = origin_cell();
    const int32_t i0 = std::max((int32_t)std::floor((p_point.x - p_radius) / dx) - oc.x, 0);
    const int32_t i1 = std::min((int32_t)std::floor((p_point.x + p_radius) / dx) - oc.x, n - 1);
    const int32_t j0 = std::max((int32_t)std::floor((p_point.z - p_radius) / dx) - oc.y, 0);
    const int32_t j1 = std::min((int32_t)std::floor((p_point.z + p_radius) / dx) - oc.y, n - 1);
    const auto kick = [&](float p_x, float p_z, bool p_x_axis) {
        const float ddx = p_x - p_point.x;
        const float ddz = p_z - p_point.z;
        const float dist = std::sqrt(ddx * ddx + ddz * ddz);
        if (dist >= p_radius || dist < 1.0e-4f) return 0.0f;
        return (p_x_axis ? ddx : ddz) / dist * p_strength * SPLASH_KICK * (1.0f - dist / p_radius);
    };
    for (int32_t j = j0; j <= j1; j++) {
        for (int32_t i = i0; i <= i1; i++) {
            Tile& t = tiles[slot_of(ivec2(i / TILE, j / TILE))];
            const int32_t c = at(i, j);
            if (c < 0 || !t.active) continue;
            const float x = (float)(oc.x + i) * dx;
            const float z = (float)(oc.y + j) * dx;
            _event(U, c, 0.0f, 0.0f, kick(x, z + 0.5f * dx, true));
            _event(W, c, 0.0f, 0.0f, kick(x + 0.5f * dx, z, false));
            if (h[c] > EPS) {
                const float ddx = x + 0.5f * dx - p_point.x;
                const float ddz = z + 0.5f * dx - p_point.z;
                const float dist = std::sqrt(ddx * ddx + ddz * ddz);
                if (dist < p_radius) _event(FOAM, c, 0.0f, 0.0f, SPLASH_FOAM * p_strength * (1.0f - dist / p_radius));
            }
        }
    }
}

static uint32_t _flow_color(float p_speed, float p_foam)
{
    static const vec3 SLOW(0x28, 0x50, 0xFF);
    static const vec3 FAST(0x33, 0xFF, 0xFF);
    static const vec3 FOAM_WHITE(0xFF, 0xFF, 0xFF);
    const vec3 base = mix(SLOW, FAST, std::clamp(p_speed / WaterLevel::ARROW_FAST, 0.0f, 1.0f));
    const uvec3 c = uvec3(mix(base, FOAM_WHITE, std::clamp(p_foam, 0.0f, 1.0f)) + 0.5f);
    return debug_rgb((c.x << 16) | (c.y << 8) | c.z);
}

void WaterLevel::debug_draw_flow(DebugDraw& r_draw, const vec4* p_skip, const vec3& p_eye) const
{
    if (!placed) return;
    const int32_t every = std::max(1, (int32_t)std::lround(ARROW_SPACING / dx));
    const float spacing = (float)every * dx;
    const auto draw = [&](ivec2 p_global, float p_h, float p_b, vec2 p_v, float p_foam) {
        if (p_h <= WET || p_b >= WALL_TEST) return;
        if ((p_global.x % every + every) % every != 0 || (p_global.y % every + every) % every != 0) return;
        const float x = ((float)p_global.x + 0.5f) * dx;
        const float z = ((float)p_global.y + 0.5f) * dx;
        if (p_skip && x >= p_skip->x && z >= p_skip->y && x < p_skip->z && z < p_skip->w) return;
        const vec3 p(x, p_h + p_b + 0.02f, z);
        const float speed = length(p_v);
        const uint32_t color = _flow_color(speed, p_foam);
        if (speed < ARROW_MIN_SPEED) {
            r_draw.point(p, 2.0f, { .color = color });
            return;
        }
        const float len = std::min(speed * ARROW_TIME, spacing * 1.8f);
        r_draw.arrow_facing(p, p + vec3(p_v.x, 0.0f, p_v.y) / speed * len, p_eye, { .color = color });
    };
    for (int32_t y = 0; y < tiles_w; y++) {
        for (int32_t x = 0; x < tiles_w; x++) {
            const Tile& t = tiles[slot_of(ivec2(x, y))];
            if (!t.active || t.phys == NO_PHYS) continue;
            const size_t o = (size_t)t.phys * TILE_CELLS;
            for (int32_t k = 0; k < TILE_CELLS; k++) draw(t.global * TILE + ivec2(k % TILE, k / TILE), h[o + k], b[o + k], vec2(uc[o + k], wc[o + k]), foam[o + k]);
        }
    }
    for (const auto& [key, f] : frozen) {
        if (!f.state || !f.has_surface) continue;
        const ivec2 g = key_tile(key);
        for (int32_t k = 0; k < TILE_CELLS; k++) draw(g * TILE + ivec2(k % TILE, k / TILE), f.state->h[k], f.state->b[k], vec2(f.state->u[k], f.state->w[k]), f.state->foam[k]);
    }
}

}
