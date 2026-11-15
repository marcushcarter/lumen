#include <core/physics/collision_preview.h>
#include <core/assets/lmesh.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace lumen {

const CollisionMeshCache::Entry* CollisionMeshCache::get(Guid p_mesh, const std::filesystem::path& p_bin)
{
    if (!p_mesh.valid()) return nullptr;
    Entry& e = entries[p_mesh];
    const auto now = std::chrono::steady_clock::now();
    if (e.polled && std::chrono::duration<double>(now - e.poll).count() < POLL_S) return e.loaded ? &e : nullptr;
    e.polled = true;
    e.poll = now;

    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(p_bin, ec);
    if (ec) {
        e.loaded = false;
        return nullptr;
    }
    if (t == e.time) return e.loaded ? &e : nullptr;
    e.time = t;
    std::vector<uint8_t> payload;
    e.loaded = lmesh_read_collision(p_bin, payload) && e.data.from_payload(payload.data(), payload.size()) == Error::OK;
    e.edges.clear();
    if (!e.loaded) return nullptr;
    collision_edges(e.data, e.edges);
    return &e;
}

mat4 collision_shape_matrix(const CollisionShapeEntry& p_shape)
{
    return translate(mat4(1.0f), p_shape.position) * mat4_cast(quat(radians(p_shape.rotation)));
}

uint32_t collision_color(const CollisionComponent& p_collision)
{
    if (p_collision.trigger) return DebugDraw::YELLOW;
    switch (p_collision.motion) {
        case PhysicsMotion::DYNAMIC: return DebugDraw::ORANGE;
        case PhysicsMotion::KINEMATIC: return DebugDraw::CYAN;
        default: return DebugDraw::GREEN;
    }
}

void collision_edges(const LCollision& p_collision, std::vector<uint32_t>& r_edges)
{
    r_edges.clear();
    std::vector<uint64_t> keys;
    keys.reserve(p_collision.indices.size());
    for (size_t i = 0; i + 2 < p_collision.indices.size(); i += 3) {
        for (int k = 0; k < 3; k++) {
            const uint32_t a = p_collision.indices[i + k];
            const uint32_t b = p_collision.indices[i + (k + 1) % 3];
            keys.push_back(((uint64_t)std::min(a, b) << 32) | std::max(a, b));
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    r_edges.reserve(keys.size() * 2);
    for (uint64_t k : keys) {
        r_edges.push_back((uint32_t)(k >> 32));
        r_edges.push_back((uint32_t)k);
    }
}

static float _max_scale(const mat4& p_m)
{
    return std::max(length(vec3(p_m[0])), std::max(length(vec3(p_m[1])), length(vec3(p_m[2]))));
}

void debug_draw_lcollision(DebugDraw& r_draw, const mat4& p_transform, const LCollision& p_collision, const std::vector<uint32_t>& p_edges, const DebugDraw::Style& p_style, bool p_meshes)
{
    const LCollision& c = p_collision;
    const float scale = _max_scale(p_transform);
    switch (c.type) {
        case CollisionType::BOX:
            r_draw.box(p_transform * translate(mat4(1.0f), c.center) * mat4_cast(c.rotation), c.half_extents, p_style);
            return;
        case CollisionType::SPHERE:
            r_draw.sphere(vec3(p_transform * vec4(c.center, 1.0f)), c.radius * scale, p_style);
            return;
        case CollisionType::CAPSULE: {
            const vec3 axis = c.rotation * vec3(0.0f, 1.0f, 0.0f);
            r_draw.capsule(vec3(p_transform * vec4(c.center - axis * c.half_height, 1.0f)), vec3(p_transform * vec4(c.center + axis * c.half_height, 1.0f)), c.radius * scale, p_style);
            return;
        }
        case CollisionType::MESH:
            if (!p_meshes) return;
            break;
        default:
            break;
    }
    for (size_t i = 0; i + 1 < p_edges.size(); i += 2) {
        r_draw.line(vec3(p_transform * vec4(c.vertices[p_edges[i]], 1.0f)), vec3(p_transform * vec4(c.vertices[p_edges[i + 1]], 1.0f)), p_style);
    }
}

void debug_draw_collision_shape(DebugDraw& r_draw, const mat4& p_world, const CollisionShapeEntry& p_shape, const CollisionMeshCache::Entry* p_model, const DebugDraw::Style& p_style, bool p_meshes)
{
    const mat4 m = p_world * collision_shape_matrix(p_shape);
    const float scale = _max_scale(p_world);
    switch (p_shape.type) {
        case CollisionShapeType::BOX:
            r_draw.box(m, p_shape.half_extents, p_style);
            return;
        case CollisionShapeType::SPHERE:
            r_draw.sphere(vec3(m[3]), p_shape.radius * scale, p_style);
            return;
        case CollisionShapeType::CAPSULE:
            r_draw.capsule(vec3(m * vec4(0.0f, -p_shape.half_height, 0.0f, 1.0f)), vec3(m * vec4(0.0f, p_shape.half_height, 0.0f, 1.0f)), p_shape.radius * scale, p_style);
            return;
        case CollisionShapeType::MODEL:
            if (p_model) debug_draw_lcollision(r_draw, m, p_model->data, p_model->edges, p_style, p_meshes);
            return;
        default:
            return;
    }
}

}
