#include <core/water/water_world.h>
#include <core/scene/scene.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace lumen {

using namespace water;

static uint64_t g_water_counter = 0;

WaterWorld::WaterWorld()
{
    sim.gpu = &link;
    sim.heights = &heights;
    link.session = ++g_water_counter;
    link.frozen.layout = ++g_water_counter;
}

void WaterWorld::clear()
{
    sim.clear();
    for (auto& m : nodes) m.clear();
    _pool_free.clear();
    _pool_capacity = 0;
    link.session = ++g_water_counter;
    link.clear_commands();
    link.page.clear();
    link.awake.clear();
    link.surface.clear();
    link.sources.clear();
    link.capacity = 0;
    link.frozen.capacity = 0;
    link.frozen.layout = ++g_water_counter;
    link.items.clear();
    link.results.clear();
    link.stats.clear();
    link.steps = 0;
    enabled = false;
    _bake_loaded.clear();
}

void WaterWorld::prepare(const Scene& p_world, const vec3& p_focus, const TerrainSource* p_terrain, const PhysicsWorld& p_physics)
{
    settings = p_world.water;
    focus = p_focus;
    enabled = true;
    const float cell = std::clamp(settings.cell_size, 0.1f, 2.0f);
    heights.bind(p_terrain);
    if (bake.active()) {
        if (heights.valid()) {
            const vec4 r = heights.rect();
            focus = vec3(0.5f * (r.x + r.z), p_focus.y, 0.5f * (r.y + r.w));
            settings.sim_radius = std::min(0.5f * std::max(r.z - r.x, r.w - r.y) + BAKE_MARGIN, BAKE_RADIUS_MAX);
        }
        settings.simulate = true;
    }
    const int32_t tiles = std::max((int32_t)std::ceil(2.0f * settings.sim_radius / (cell * (float)TILE)), 4);
    const std::filesystem::path want = bake.active() ? std::filesystem::path{} : bake_path;
    if (want != _bake_loaded && sim.placed) sim.clear();
    const uint64_t layout = sim.layout;
    sim.configure(cell, tiles);
    if (sim.layout != layout) {
        for (auto& m : nodes) m.clear();
        _pool_reset();
    }
    std::vector<uint64_t> baked;
    if (want != _bake_loaded || (!want.empty() && _bake_layout != sim.layout)) {
        _bake_loaded = want;
        _bake_layout = sim.layout;
        if (!want.empty() && !sim.load_bake(want, baked)) log_write("Water: couldn't load bake '%s'.", want.string().c_str());
    }
    for (const WaterGpuResult& r : link.results) sim.consume(r);
    link.results.clear();
    sim.consume_stats(link.stats_layout, link.stats);
    link.stats.clear();
    sim.prepare(p_world, focus, p_terrain, p_physics);
    sim.frozen_added.insert(sim.frozen_added.end(), baked.begin(), baked.end());
    _sync_frozen();
    _bake_update();
    if (++_frame % PAGE_INTERVAL == 0) sim.page_out(settings.sim_radius + PAGE_DISTANCE);
}

float WaterBake::progress() const
{
    if (phase == Phase::RUN) return seconds > 0.0f ? std::clamp(elapsed / seconds, 0.0f, 1.0f) * 0.95f : 0.0f;
    if (phase == Phase::COLLECT) return 0.95f + 0.05f * (collect_needed ? std::clamp((float)collect_frames / (float)collect_needed, 0.0f, 1.0f) : 0.0f);
    return phase == Phase::DONE ? 1.0f : 0.0f;
}

void WaterBake::start(Guid p_guid, const std::filesystem::path& p_path, float p_seconds)
{
    phase = Phase::RUN;
    guid = p_guid;
    path = p_path;
    seconds = p_seconds;
    elapsed = 0.0f;
    collect_frames = 0;
    collect_needed = 0;
    message.clear();
    result = LWater{};
}

void WaterBake::cancel()
{
    if (!active()) return;
    phase = Phase::IDLE;
    message = "Bake cancelled";
}

void WaterWorld::_bake_update()
{
    if (!bake.active()) {
        link.max_steps = WaterGpuLink::MAX_STEPS;
        link.collect = false;
        return;
    }
    if (bake.phase == WaterBake::Phase::RUN) {
        link.max_steps = WaterGpuLink::BAKE_STEPS;
        link.collect = false;
        if (!sim.placed) return;
        link.steps = WaterGpuLink::BAKE_STEPS;
        const float dt = (float)WaterGpuLink::BAKE_STEPS / 60.0f;
        link.time += dt;
        bake.elapsed += dt;
        if (bake.elapsed < bake.seconds) return;
        bake.phase = WaterBake::Phase::COLLECT;
        bake.collect_frames = 0;
    }
    link.max_steps = 0;
    link.steps = 0;
    link.collect = true;
    bake.collect_needed = (uint32_t)((link.awake.size() + WaterGpuLink::READBACK_TILES - 1) / WaterGpuLink::READBACK_TILES) + BAKE_LAG_FRAMES;
    if (++bake.collect_frames < bake.collect_needed) return;
    link.collect = false;
    link.max_steps = WaterGpuLink::MAX_STEPS;
    if (_bake_write()) {
        bake.phase = WaterBake::Phase::DONE;
        bake.message = "Baked " + std::to_string(bake.result.tiles) + " tiles";
    } else {
        bake.phase = WaterBake::Phase::FAILED;
        bake.message = "Bake failed to write";
    }
    log_write("Water: %s (%s)", bake.message.c_str(), bake.path.string().c_str());
}

bool WaterWorld::_bake_write()
{
    struct Block {
        vec4 surface[SURFACE_SAMPLES];
        uint32_t mask[SURFACE_MASK_WORDS];
        WaterLevel::FrozenState state;
    };
    std::vector<LWaterTileEntry> entries;
    std::vector<std::unique_ptr<Block>> blocks;
    float wet_cells = 0.0f;
    const auto add = [&](ivec2 p_tile, const WaterLevel::FrozenState& p_state) {
        uint32_t wet = 0;
        for (int32_t k = 0; k < TILE_CELLS; k++) wet += p_state.h[k] > WET && p_state.b[k] < WALL_TEST ? 1u : 0u;
        if (!wet) return;
        auto block = std::make_unique<Block>();
        block->state = p_state;
        LWaterTileEntry e{};
        e.tile_x = p_tile.x;
        e.tile_y = p_tile.y;
        e.has_surface = sim.surface_patch(p_tile * TILE, block->surface, block->mask, e.ymin, e.ymax) ? 1u : 0u;
        entries.push_back(e);
        blocks.push_back(std::move(block));
        wet_cells += (float)wet;
    };
    for (int32_t y = 0; y < sim.tiles_w; y++) {
        for (int32_t x = 0; x < sim.tiles_w; x++) {
            const WaterLevel::Tile& t = sim.tiles[sim.slot_of(ivec2(x, y))];
            if (!t.active || t.phys == WaterLevel::NO_PHYS) continue;
            const size_t o = (size_t)t.phys * TILE_CELLS;
            auto st = std::make_unique<WaterLevel::FrozenState>();
            std::memcpy(st->h, sim.h.data() + o, sizeof(st->h));
            std::memcpy(st->b, sim.b.data() + o, sizeof(st->b));
            std::memcpy(st->foam, sim.foam.data() + o, sizeof(st->foam));
            std::memcpy(st->u, sim.u.data() + o, sizeof(st->u));
            std::memcpy(st->w, sim.w.data() + o, sizeof(st->w));
            std::memcpy(st->flags, sim.flags.data() + o, sizeof(st->flags));
            add(t.global, *st);
        }
    }
    for (auto& [key, f] : sim.frozen) {
        const ivec2 g = key_tile(key);
        const ivec2 l = g - sim.origin_tile;
        if (sim.in_interior(l) && sim.tiles[sim.slot_of(l)].global == g && sim.tiles[sim.slot_of(l)].active) continue;
        if (!sim._load_page(f) || !f.state) continue;
        add(g, *f.state);
    }

    LWaterPayloadHeader header{};
    header.format_version = LWATER_FORMAT_VERSION;
    header.tile_count = (uint32_t)entries.size();
    header.cell_size = sim.dx;
    header.directory_offset = sizeof(LAssetHeader) + sizeof(LWaterPayloadHeader);
    header.state_offset = header.directory_offset + entries.size() * sizeof(LWaterTileEntry);
    for (size_t k = 0; k < entries.size(); k++) entries[k].state_offset = header.state_offset + k * sizeof(Block);

    std::error_code ec;
    std::filesystem::create_directories(bake.path.parent_path(), ec);
    const std::filesystem::path tmp = bake.path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const LAssetHeader ah{ BCON_MAGIC, LASSET_VERSION, bake.guid, AssetType::WATER, 0 };
        out.write(reinterpret_cast<const char*>(&ah), sizeof(ah));
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!entries.empty()) out.write(reinterpret_cast<const char*>(entries.data()), (std::streamsize)(entries.size() * sizeof(LWaterTileEntry)));
        for (const auto& b : blocks) out.write(reinterpret_cast<const char*>(b.get()), sizeof(Block));
        if (!out) return false;
    }
    std::filesystem::rename(tmp, bake.path, ec);
    if (ec) return false;

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &local);
    bake.result.guid = bake.guid;
    bake.result.terrain = heights.guid;
    bake.result.cell_size = sim.dx;
    bake.result.tiles = (uint32_t)entries.size();
    bake.result.wet_area = wet_cells * sim.dx * sim.dx;
    bake.result.bake_seconds = bake.seconds;
    bake.result.file_bytes = (uint64_t)std::filesystem::file_size(bake.path, ec);
    bake.result.baked_at = stamp;
    return true;
}

void WaterWorld::preview(const WaterSettings& p_settings, const std::filesystem::path& p_bake, float p_dt)
{
    settings = p_settings;
    enabled = true;
    const float cell = std::clamp(settings.cell_size, 0.1f, 2.0f);
    const int32_t tiles = std::max((int32_t)std::ceil(2.0f * settings.sim_radius / (cell * (float)TILE)), 4);
    if (p_bake != _bake_loaded && !sim.tiles.empty()) sim.clear();
    const uint64_t layout = sim.layout;
    sim.configure(cell, tiles);
    if (sim.layout != layout) {
        for (auto& m : nodes) m.clear();
        _pool_reset();
    }
    if (p_bake != _bake_loaded || _bake_layout != sim.layout) {
        _bake_loaded = p_bake;
        _bake_layout = sim.layout;
        std::vector<uint64_t> baked;
        if (!p_bake.empty() && !sim.load_bake(p_bake, baked)) log_write("Water: couldn't load bake '%s'.", p_bake.string().c_str());
        sim.frozen_added.insert(sim.frozen_added.end(), baked.begin(), baked.end());
    }
    _sync_frozen();
    link.steps = 0;
    link.max_steps = WaterGpuLink::MAX_STEPS;
    link.collect = false;
    link.time += p_dt;
}

void WaterWorld::step()
{
    link.steps = std::min(link.steps + 1, link.max_steps);
    link.time += 1.0f / 60.0f;
}

void WaterWorld::_pool_reset()
{
    _pool_free.clear();
    _pool_capacity = 0;
    link.frozen.clear_commands();
    link.frozen.capacity = 0;
    link.frozen.layout = ++g_water_counter;
}

uint32_t WaterWorld::_pool_alloc()
{
    if (_pool_free.empty()) {
        const uint32_t grown = std::max(POOL_MIN, _pool_capacity * 2);
        for (uint32_t p = grown; p-- > _pool_capacity;) _pool_free.push_back(p);
        _pool_capacity = grown;
        link.frozen.capacity = grown;
        link.frozen.layout = ++g_water_counter;
        link.frozen.clear_commands();
        for (auto& [key, f] : sim.frozen) if (f.pool != WaterLevel::NO_POOL) _pool_upload(f.pool, f.surface, f.mask);
        for (auto& m : nodes) for (auto& [key, s] : m) if (s.pool != WaterLevel::NO_POOL) _pool_upload(s.pool, s.samples, s.mask);
    }
    const uint32_t p = _pool_free.back();
    _pool_free.pop_back();
    return p;
}

void WaterWorld::_pool_upload(uint32_t p_slot, const vec4* p_samples, const uint32_t* p_mask)
{
    link.frozen.slots.push_back(p_slot);
    link.frozen.samples.insert(link.frozen.samples.end(), p_samples, p_samples + SURFACE_SAMPLES);
    link.frozen.masks.insert(link.frozen.masks.end(), p_mask, p_mask + SURFACE_MASK_WORDS);
}

void WaterWorld::_sync_frozen()
{
    const auto touch = [&](uint64_t p_key) {
        ivec2 t = key_tile(p_key);
        for (int32_t k = 1; k <= TREE_LEVELS; k++) {
            t = ivec2(floor_div(t.x, BRANCH), floor_div(t.y, BRANCH));
            nodes[k][tile_key(t)].dirty = true;
        }
    };
    for (const uint64_t key : sim.frozen_added) {
        const auto it = sim.frozen.find(key);
        if (it == sim.frozen.end()) continue;
        WaterLevel::Frozen& f = it->second;
        if (f.has_surface) {
            if (f.pool == WaterLevel::NO_POOL) f.pool = _pool_alloc();
            _pool_upload(f.pool, f.surface, f.mask);
        } else if (f.pool != WaterLevel::NO_POOL) {
            _pool_free.push_back(f.pool);
            f.pool = WaterLevel::NO_POOL;
        }
        touch(key);
    }
    sim.frozen_added.clear();
    for (const auto& [key, pool] : sim.frozen_removed) {
        if (pool != WaterLevel::NO_POOL) _pool_free.push_back(pool);
        touch(key);
    }
    sim.frozen_removed.clear();
    for (int32_t k = 1; k <= TREE_LEVELS; k++) {
        for (auto it = nodes[k].begin(); it != nodes[k].end();) {
            Node& n = it->second;
            if (n.dirty) _rebuild_node(k, it->first, n);
            if (!n.has) {
                if (n.pool != WaterLevel::NO_POOL) _pool_free.push_back(n.pool);
                it = nodes[k].erase(it);
                continue;
            }
            ++it;
        }
    }
}

bool WaterWorld::_child(int32_t p_level, ivec2 p_key, const vec4*& r_samples, const uint32_t*& r_mask, float& r_ymin, float& r_ymax) const
{
    if (p_level == 0) {
        const auto it = sim.frozen.find(tile_key(p_key));
        if (it == sim.frozen.end() || !it->second.has_surface) return false;
        r_samples = it->second.surface;
        r_mask = it->second.mask;
        r_ymin = it->second.ymin;
        r_ymax = it->second.ymax;
        return true;
    }
    const auto it = nodes[p_level].find(tile_key(p_key));
    if (it == nodes[p_level].end() || !it->second.has) return false;
    r_samples = it->second.samples;
    r_mask = it->second.mask;
    r_ymin = it->second.ymin;
    r_ymax = it->second.ymax;
    return true;
}

void WaterWorld::_rebuild_node(int32_t p_level, uint64_t p_key, Node& r_node)
{
    r_node.dirty = false;
    r_node.ymin = FLT_MAX;
    r_node.ymax = -FLT_MAX;
    std::fill_n(r_node.mask, SURFACE_MASK_WORDS, 0u);
    const ivec2 base = key_tile(p_key) * BRANCH;
    const vec4* samples = nullptr;
    const uint32_t* mask = nullptr;
    float lo = 0.0f, hi = 0.0f;
    bool any = false;
    for (int32_t y = 0; y < BRANCH; y++) {
        for (int32_t x = 0; x < BRANCH; x++) {
            if (!_child(p_level - 1, base + ivec2(x, y), samples, mask, lo, hi)) continue;
            any = true;
            r_node.ymin = std::min(r_node.ymin, lo);
            r_node.ymax = std::max(r_node.ymax, hi);
        }
    }
    const int32_t step = TILE / BRANCH;
    for (int32_t sy = 0; sy < SURFACE_ROW; sy++) {
        for (int32_t sx = 0; sx < SURFACE_ROW; sx++) {
            const ivec2 g = ivec2(sx, sy) * BRANCH;
            const ivec2 c(floor_div(g.x, TILE), floor_div(g.y, TILE));
            const ivec2 l = g - c * TILE;
            vec4 s(0.0f, SAMPLE_INVALID, 0.0f, 0.0f);
            if (_child(p_level - 1, base + c, samples, mask, lo, hi)) s = samples[l.y * SURFACE_ROW + l.x];
            r_node.samples[sy * SURFACE_ROW + sx] = s;
        }
    }
    for (int32_t cy = 0; cy < TILE; cy++) {
        for (int32_t cx = 0; cx < TILE; cx++) {
            const ivec2 c(cx / step, cy / step);
            if (!_child(p_level - 1, base + c, samples, mask, lo, hi)) continue;
            const ivec2 l = ivec2(cx % step, cy % step) * BRANCH;
            bool on = false;
            for (int32_t y = 0; y < BRANCH && !on; y++) {
                for (int32_t x = 0; x < BRANCH && !on; x++) {
                    const int32_t k = (l.y + y) * TILE + l.x + x;
                    on = (mask[k >> 5] >> (k & 31)) & 1u;
                }
            }
            if (!on) continue;
            const int32_t k = cy * TILE + cx;
            r_node.mask[k >> 5] |= 1u << (k & 31);
        }
    }
    r_node.has = any && r_node.ymin <= r_node.ymax;
    if (!r_node.has) return;
    if (r_node.pool == WaterLevel::NO_POOL) r_node.pool = _pool_alloc();
    _pool_upload(r_node.pool, r_node.samples, r_node.mask);
}

void WaterWorld::_visit(int32_t p_level, uint64_t p_key, const vec2& p_camera)
{
    const ivec2 key = key_tile(p_key);
    float span = sim.dx * (float)TILE;
    for (int32_t k = 0; k < p_level; k++) span *= (float)BRANCH;
    const vec2 lo = vec2(key) * span;
    if (p_level == 0) {
        const auto it = sim.frozen.find(p_key);
        if (it == sim.frozen.end() || !it->second.has_surface || it->second.pool == WaterLevel::NO_POOL) return;
        link.items.push_back({ WaterGpuItem::FROZEN, it->second.pool, lo, sim.dx, it->second.ymin, it->second.ymax });
        return;
    }
    const auto it = nodes[p_level].find(p_key);
    if (it == nodes[p_level].end() || !it->second.has) return;
    const Node& n = it->second;
    const float spacing = span / (float)TILE;
    const float d = length(clamp(p_camera, lo, lo + vec2(span)) - p_camera);
    if (d * link.lod_error >= spacing && n.pool != WaterLevel::NO_POOL) {
        link.items.push_back({ WaterGpuItem::FROZEN, n.pool, lo, spacing, n.ymin, n.ymax });
        return;
    }
    for (int32_t y = 0; y < BRANCH; y++) for (int32_t x = 0; x < BRANCH; x++) _visit(p_level - 1, tile_key(key * BRANCH + ivec2(x, y)), p_camera);
}

void WaterWorld::publish(const vec3& p_camera)
{
    sim.publish();
    link.items.clear();
    link.lod_error = std::clamp(settings.lod_error, 0.001f, 0.2f);
    link.min_quad = sim.dx * 0.5f;
    link.caustics = settings.caustics;
    if (!enabled || !settings.render) return;
    if (sim.placed) {
        for (int32_t y = 0; y < sim.tiles_w; y++) {
            for (int32_t x = 0; x < sim.tiles_w; x++) {
                const WaterLevel::Tile& t = sim.tiles[sim.slot_of(ivec2(x, y))];
                if (!t.active || t.phys == WaterLevel::NO_PHYS || !t.surface_built) continue;
                link.items.push_back({ WaterGpuItem::LIVE, t.phys, vec2(t.global * TILE) * sim.dx, sim.dx, 0.0f, 0.0f });
            }
        }
    }
    const vec2 cam(p_camera.x, p_camera.z);
    for (const auto& [key, n] : nodes[TREE_LEVELS]) _visit(TREE_LEVELS, key, cam);
}

void WaterWorld::static_changed(const vec3& p_min, const vec3& p_max)
{
    sim.static_changed(p_min, p_max);
}

void WaterWorld::debug_draw_flow(DebugDraw& r_draw, const vec3& p_eye) const
{
    if (enabled) sim.debug_draw_flow(r_draw, nullptr, p_eye);
}

WaterSample WaterWorld::query(float p_x, float p_z) const
{
    if (!enabled) return {};
    return sim.query(p_x, p_z);
}

bool WaterWorld::sample(float p_x, float p_z, float& r_surface_y, vec2& r_flow) const
{
    const WaterSample q = query(p_x, p_z);
    if (!q.valid || q.h < BUOYANCY_MIN_DEPTH) return false;
    r_surface_y = q.eta;
    r_flow = vec2(q.u, q.w);
    return true;
}

void WaterWorld::push(const vec3& p_point, float p_submerged, float p_volume, const vec3& p_velocity, const vec3& p_relative)
{
    if (enabled) sim.push(p_point, p_submerged, p_volume, p_velocity, p_relative);
}

void WaterWorld::splash(const vec3& p_point, float p_strength, float p_radius)
{
    if (enabled) sim.splash(p_point, p_strength, p_radius);
}

}
