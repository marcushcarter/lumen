#pragma once
#include <core/assets/guid.h>
#include <core/assets/lcollision.h>
#include <core/rendering/debug_draw.h>
#include <core/world/components.h>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct World;
struct Renderer;
struct Project;

struct CollisionPreview
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

    static uint32_t color(const CollisionComponent& p_collision);
    static void draw_model(DebugDraw& r_draw, const mat4& p_transform, const Entry& p_entry, const DebugDraw::Style& p_style);
    static void draw_shape(DebugDraw& r_draw, const mat4& p_world, const CollisionShapeEntry& p_shape, const Entry* p_model, const DebugDraw::Style& p_style);
    void draw_world(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer, const Project& p_project);
};

mat4 collision_shape_matrix(const CollisionShapeEntry& p_shape);

}
