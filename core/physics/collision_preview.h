#pragma once
#include <core/assets/guid.h>
#include <core/assets/lcollision.h>
#include <core/rendering/debug_draw.h>
#include <core/scene/components.h>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct CollisionMeshCache
{
    static constexpr double POLL_S = 1.0;

    struct Entry {
        LCollision data;
        std::vector<uint32_t> edges;
        std::filesystem::file_time_type time{};
        std::chrono::steady_clock::time_point poll{};
        bool loaded = false;
        bool polled = false;
    };

    std::unordered_map<Guid, Entry> entries;

    const Entry* get(Guid p_mesh, const std::filesystem::path& p_bin);
    void clear() { entries.clear(); }
};

mat4 collision_shape_matrix(const CollisionShapeEntry& p_shape);
uint32_t collision_color(const CollisionComponent& p_collision);
void collision_edges(const LCollision& p_collision, std::vector<uint32_t>& r_edges);
void debug_draw_lcollision(DebugDraw& r_draw, const mat4& p_transform, const LCollision& p_collision, const std::vector<uint32_t>& p_edges, const DebugDraw::Style& p_style, bool p_meshes);
void debug_draw_collision_shape(DebugDraw& r_draw, const mat4& p_world, const CollisionShapeEntry& p_shape, const CollisionMeshCache::Entry* p_model, const DebugDraw::Style& p_style, bool p_meshes);

}
