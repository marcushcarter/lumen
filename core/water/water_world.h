#pragma once
#include <core/water/water_level.h>
#include <core/physics/buoyancy.h>
#include <core/rendering/water_gpu.h>
#include <core/assets/lwater.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct WaterBake
{
    enum class Phase { IDLE, RUN, COLLECT, DONE, FAILED };

    Phase phase = Phase::IDLE;
    Guid guid;
    std::filesystem::path path;
    float seconds = 0.0f;
    float elapsed = 0.0f;
    uint32_t collect_frames = 0;
    uint32_t collect_needed = 0;
    std::string message;
    LWater result;

    bool active() const { return phase == Phase::RUN || phase == Phase::COLLECT; }
    bool collecting() const { return phase == Phase::COLLECT; }
    float progress() const;
    void start(Guid p_guid, const std::filesystem::path& p_path, float p_seconds);
    void cancel();
};

struct WaterWorld : WaterQuery
{
    static constexpr float BAKE_MARGIN = 8.0f;
    static constexpr float BAKE_RADIUS_MAX = 2048.0f;
    static constexpr uint32_t BAKE_LAG_FRAMES = 6;
    static constexpr float RIPPLE_MIN_SPEED = 0.05f;
    static constexpr float WADE_RADIUS = 0.35f;
    static constexpr float WADE_IDLE = 0.15f;
    static constexpr float WADE_SPEED_GAIN = 0.8f;
    static constexpr float BUOYANCY_MIN_DEPTH = 0.02f;
    static constexpr int32_t BRANCH = 4;
    static constexpr int32_t TREE_LEVELS = 3;
    static constexpr uint32_t POOL_MIN = 1024;
    static constexpr uint32_t PAGE_INTERVAL = 120;
    static constexpr float PAGE_DISTANCE = 512.0f;

    struct Node {
        vec4 samples[water::SURFACE_SAMPLES];
        uint32_t normals[water::SURFACE_SAMPLES] = {};
        uint32_t mask[water::SURFACE_MASK_WORDS] = {};
        float ymin = 0.0f;
        float ymax = 0.0f;
        uint32_t pool = WaterLevel::NO_POOL;
        bool dirty = true;
        bool has = false;
    };

    WaterTerrainHeights heights;
    WaterLevel sim;
    WaterGpuLink link;
    WaterSettings settings;
    WaterBake bake;
    std::filesystem::path bake_path;
    std::filesystem::path _bake_loaded;
    uint64_t _bake_layout = 0;
    vec3 focus = vec3(0.0f);
    std::unordered_map<uint64_t, Node> nodes[TREE_LEVELS + 1];
    std::vector<uint32_t> _pool_free;
    uint32_t _pool_capacity = 0;
    uint64_t _frame = 0;
    bool enabled = false;

    WaterWorld();
    void clear();
    void prepare(const Scene& p_world, const vec3& p_focus, const TerrainSource* p_terrain, const PhysicsWorld& p_physics);
    void step();
    void preview(const WaterSettings& p_settings, const std::filesystem::path& p_bake, float p_dt);
    void publish(const vec3& p_camera);
    void static_changed(const vec3& p_min, const vec3& p_max);
    void debug_draw_flow(DebugDraw& r_draw, const vec3& p_eye) const;

    WaterSample query(float p_x, float p_z) const;
    bool sample(float p_x, float p_z, float& r_surface_y, vec2& r_flow) const override;
    void push(const vec3& p_point, float p_submerged, float p_volume, const vec3& p_velocity, const vec3& p_relative) override;
    void splash(const vec3& p_point, float p_strength, float p_radius) override;
    void wade(const vec3& p_position, const vec3& p_velocity);
    void _ripple(const vec2& p_xz, float p_radius, float p_strength);

    uint32_t _pool_alloc();
    void _pool_upload(uint32_t p_slot, const vec4* p_samples, const uint32_t* p_normals, const uint32_t* p_mask);
    void _pool_reset();
    void _sync_frozen();
    void _rebuild_node(int32_t p_level, uint64_t p_key, Node& r_node);
    bool _child(int32_t p_level, ivec2 p_key, const vec4*& r_samples, const uint32_t*& r_normals, const uint32_t*& r_mask, float& r_ymin, float& r_ymax) const;
    void _visit(int32_t p_level, uint64_t p_key, const vec2& p_camera);
    void _bake_update();
    bool _bake_write();
};

}
