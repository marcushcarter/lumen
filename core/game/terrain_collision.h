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

    struct Field {
        uint64_t key = 0;
        Guid guid;
        vec3 position = vec3(0.0f);
        std::filesystem::path path;
        TerrainSource source;
        float lowest = 0.0f;
        std::vector<Tile> tiles;
        std::vector<std::shared_ptr<Build>> inflight;
    };

    std::vector<std::unique_ptr<Field>> fields;
    float lowest = 0.0f;
    std::vector<uint32_t> _order;

    bool valid() const;
    void update(PhysicsWorld& r_physics, const std::vector<TerrainInstance>& p_terrains, const std::vector<std::filesystem::path>& p_paths, const std::vector<vec3>& p_anchors);
    void clear(PhysicsWorld& r_physics);

    static void _clear_field(PhysicsWorld& r_physics, Field& r_field);
    void _update_field(PhysicsWorld& r_physics, Field& r_field, const std::vector<vec3>& p_anchors);
    static void _tile_box(const Field& p_field, uint32_t p_tile, vec2& r_min, vec2& r_max);
};

}
