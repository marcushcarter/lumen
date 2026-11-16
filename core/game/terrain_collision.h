#pragma once
#include <core/terrain/terrain_source.h>
#include <core/physics/physics_world.h>
#include <atomic>
#include <memory>
#include <vector>

namespace lumen {

struct TerrainCollision
{
    static constexpr float LOAD_RADIUS = 160.0f;
    static constexpr float UNLOAD_RADIUS = 220.0f;
    static constexpr uint32_t MAX_INFLIGHT = 4;
    static constexpr float FRICTION = 0.8f;

    struct Build {
        uint32_t tile = 0;
        PhysicsHeightField field;
        std::atomic<bool> done{ false };
    };

    struct Tile {
        PhysicsBodyId body;
        bool loading = false;
        float distance = 0.0f;
    };

    TerrainSource source;
    float lowest = 0.0f;
    std::vector<Tile> tiles;
    std::vector<std::shared_ptr<Build>> inflight;
    std::vector<uint32_t> _order;

    void update(PhysicsWorld& r_physics, Guid p_terrain, const std::filesystem::path& p_path, const std::vector<vec3>& p_anchors);
    void clear(PhysicsWorld& r_physics);

    void _wait();
    void _tile_box(uint32_t p_tile, vec2& r_min, vec2& r_max) const;
    static void _build(const TerrainSource& p_source, uint32_t p_tile, const std::filesystem::path& p_path, uint64_t p_offset, Build& r_build);
};

}
