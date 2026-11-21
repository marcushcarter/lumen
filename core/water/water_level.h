#pragma once
#include <core/water/water_grid.h>
#include <core/rendering/water_gpu.h>
#include <core/scene/scene_settings.h>
#include <core/water/water_terrain.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace lumen {

struct PhysicsWorld;
struct Scene;
struct DebugDraw;
struct TerrainSource;

struct WaterSample
{
    float h = 0.0f;
    float eta = 0.0f;
    float u = 0.0f;
    float w = 0.0f;
    bool valid = false;
};

struct WaterLevel
{
    static constexpr uint32_t NO_PHYS = water::PAGE_EMPTY;
    static constexpr uint32_t NO_POOL = 0xFFFFFFFFu;
    static constexpr uint32_t BED_BUDGET = 128;
    static constexpr int32_t RECENTER_TILES = 2;
    static constexpr uint32_t FRESH_FRAMES = 6;
    static constexpr float BED_MESH_MARGIN = 2.0f;
    static constexpr uint32_t BED_RETRY_FRAMES = 60;
    static constexpr float BED_RANGE = 200.0f;
    static constexpr float DRAIN_DROP = 4.0f;
    static constexpr uint32_t CAPACITY_MIN = 256;
    static constexpr float BED_CACHE_KEEP = 3.0f;
    static constexpr float PUSH_MIN_DEPTH = 0.15f;
    static constexpr float PUSH_GAIN = 0.35f;
    static constexpr float PUSH_MAX = 0.25f;
    static constexpr float PUSH_FOAM_SPEED = 1.2f;
    static constexpr float PUSH_FOAM = 0.03f;
    static constexpr float SPLASH_KICK = 1.0f;
    static constexpr float SPLASH_FOAM = 0.3f;
    static constexpr float ARROW_SPACING = 0.5f;
    static constexpr float ARROW_TIME = 0.25f;
    static constexpr float ARROW_MIN_SPEED = 0.03f;
    static constexpr float ARROW_FAST = 4.0f;

    enum class Bed : uint8_t { NONE, QUEUED, READY, MISSING };

    struct Tile {
        ivec2 global = ivec2(INT32_MIN);
        uint32_t phys = NO_PHYS;
        Bed bed = Bed::NONE;
        bool ring = false;
        bool rebuild = false;
        bool active = false;
        bool awake = false;
        bool source = false;
        bool spring = false;
        bool surface_dirty = true;
        bool surface_built = false;
        uint32_t retry = 0;
        uint32_t fresh_frames = 0;
        float max_h = 0.0f;
    };

    struct BedCache {
        float b[water::TILE_CELLS];
        uint32_t flags[water::TILE_CELLS];
        bool missing = false;
    };

    struct FrozenState {
        float h[water::TILE_CELLS];
        float b[water::TILE_CELLS];
        float foam[water::TILE_CELLS];
        float u[water::TILE_CELLS];
        float w[water::TILE_CELLS];
        uint32_t flags[water::TILE_CELLS];
    };

    struct Frozen {
        std::unique_ptr<FrozenState> state;
        Bed bed = Bed::READY;
        vec4 surface[water::SURFACE_SAMPLES];
        uint32_t mask[water::SURFACE_MASK_WORDS] = {};
        bool has_surface = false;
        float ymin = 0.0f;
        float ymax = 0.0f;
        uint64_t page_offset = UINT64_MAX;
        uint32_t pool = NO_POOL;
    };

    struct SourceCell {
        uint32_t cell = 0;
        float value = 0.0f;
        bool level = false;
    };

    struct Stats {
        uint32_t tiles = 0;
        uint32_t active = 0;
        uint32_t awake = 0;
        uint32_t frozen = 0;
        uint32_t paged = 0;
    };

    WaterTerrainHeights* heights = nullptr;
    uint32_t _surface_capacity = 0;
    WaterGpuLink* gpu = nullptr;
    WaterSettings settings;
    float gravity = 9.81f;
    float dx = 0.25f;
    int32_t tiles_w = 0;
    int32_t n = 0;
    int32_t slots = 0;
    ivec2 origin_tile = ivec2(0);
    bool placed = false;
    vec3 focus = vec3(0.0f);
    uint64_t layout = 0;
    Stats stats;

    std::vector<Tile> tiles;
    uint32_t capacity = 0;
    std::vector<uint32_t> free_phys;
    std::vector<float> h, b, foam, u, w, uc, wc;
    std::vector<uint32_t> flags;
    std::unordered_map<uint64_t, BedCache> beds;
    std::unordered_map<uint64_t, Frozen> frozen;
    std::vector<uint64_t> frozen_added;
    std::vector<std::pair<uint64_t, uint32_t>> frozen_removed;

    std::vector<ivec2> _bed_queue;
    std::vector<SourceCell> _sources;
    uint64_t _source_hash = 0;
    uint64_t _alloc_epoch = 0;
    std::filesystem::path _page_path;
    std::fstream _page_file;
    uint64_t _page_end = 0;
    std::vector<uint64_t> _page_free;

    ~WaterLevel();
    void clear();
    void configure(float p_dx, int32_t p_tiles_w);
    void prepare(const Scene& p_world, const vec3& p_focus, const TerrainSource* p_terrain, const PhysicsWorld& p_physics);
    void consume(const WaterGpuResult& p_result);
    void consume_stats(uint64_t p_layout, const std::vector<float>& p_stats);
    void publish();
    void page_out(float p_distance);
    void static_changed(const vec3& p_min, const vec3& p_max);

    bool inside(float p_x, float p_z) const;
    vec4 interior_rect() const;
    WaterSample query(float p_x, float p_z) const;
    void push(const vec3& p_point, float p_submerged, float p_volume, const vec3& p_velocity, const vec3& p_relative);
    void splash(const vec3& p_point, float p_strength, float p_radius);
    void debug_draw_flow(DebugDraw& r_draw, const vec4* p_skip, const vec3& p_eye) const;

    ivec2 origin_cell() const { return origin_tile * water::TILE; }
    int32_t slot_of(ivec2 p_local) const { return water::wrap(origin_tile.y + p_local.y, slots) * slots + water::wrap(origin_tile.x + p_local.x, slots); }
    bool in_window(ivec2 p_local) const { return p_local.x >= -1 && p_local.y >= -1 && p_local.x <= tiles_w && p_local.y <= tiles_w; }
    bool in_interior(ivec2 p_local) const { return p_local.x >= 0 && p_local.y >= 0 && p_local.x < tiles_w && p_local.y < tiles_w; }
    int32_t at(int32_t p_i, int32_t p_j) const;
    bool cell_state(ivec2 p_global, float& r_h, float& r_b) const;
    vec4 surface_sample(ivec2 p_global) const;
    bool surface_patch(ivec2 p_global, vec4* r_samples, uint32_t* r_mask, float& r_ymin, float& r_ymax) const;

    uint32_t _alloc();
    void _release(uint32_t p_phys);
    void _init_phys(uint32_t p_phys);
    void _upload(uint32_t p_array, uint32_t p_phys, const float* p_data);
    void _upload_mirror(uint32_t p_phys, bool p_all);
    void _recenter(ivec2 p_origin_tile);
    void _evict(Tile& r_tile, bool p_interior);
    void _enter(Tile& r_tile, ivec2 p_local);
    void _freeze(const Tile& r_tile);
    void _thaw(Tile& r_tile, ivec2 p_local);
    void _activate(Tile& r_tile, ivec2 p_local, const BedCache& p_bed);
    void _deactivate(Tile& r_tile);
    void _classify();
    void _build_beds(const TerrainSource* p_terrain, const PhysicsWorld& p_physics);
    float _probe_top(ivec2 p_global, const TerrainSource* p_terrain) const;
    void _gather_sources(const Scene& p_world);
    bool _load_page(Frozen& r_frozen);
    const FrozenState* _frozen_state(ivec2 p_global_cell, int32_t& r_index) const;
    void _event(uint32_t p_array, int32_t p_cell, float p_lerp, float p_target, float p_add);
};

}
