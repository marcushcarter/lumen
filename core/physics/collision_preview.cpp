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
        case CollisionType::SHAPES:
            for (const LCollisionShape& shape : c.shapes) debug_draw_lcollision_shape(r_draw, p_transform, shape, p_style);
            if (c.parts.empty() || !p_meshes) return;
            break;
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

PhysicsShape physics_shape(const CollisionShapeEntry& p_shape)
{
    switch (p_shape.type) {
        case CollisionShapeType::SPHERE: return PhysicsShape::sphere(p_shape.radius);
        case CollisionShapeType::CAPSULE: return PhysicsShape::capsule(p_shape.half_height, p_shape.radius);
        case CollisionShapeType::CYLINDER: return PhysicsShape::cylinder(p_shape.half_height, p_shape.radius);
        case CollisionShapeType::TAPERED_CYLINDER: return PhysicsShape::tapered_cylinder(p_shape.half_height, p_shape.top_radius, p_shape.radius);
        case CollisionShapeType::TAPERED_CAPSULE: return PhysicsShape::tapered_capsule(p_shape.half_height, p_shape.top_radius, p_shape.radius);
        case CollisionShapeType::WEDGE: return PhysicsShape::wedge(p_shape.half_extents);
        default: return PhysicsShape::box(p_shape.half_extents);
    }
}

PhysicsShape physics_shape(const LCollisionShape& p_shape)
{
    const vec3 half(p_shape.half_extents);
    switch (p_shape.kind()) {
        case CollisionType::SPHERE: return PhysicsShape::sphere(p_shape.radius);
        case CollisionType::CAPSULE: return PhysicsShape::capsule(p_shape.half_height, p_shape.radius);
        case CollisionType::CYLINDER: return PhysicsShape::cylinder(p_shape.half_height, p_shape.radius);
        case CollisionType::TAPERED_CYLINDER: return PhysicsShape::tapered_cylinder(p_shape.half_height, p_shape.top_radius, p_shape.radius);
        case CollisionType::TAPERED_CAPSULE: return PhysicsShape::tapered_capsule(p_shape.half_height, p_shape.top_radius, p_shape.radius);
        case CollisionType::WEDGE: return PhysicsShape::wedge(half);
        default: return PhysicsShape::box(half);
    }
}

static void _rings(DebugDraw& r_draw, const mat4& p_m, float p_scale, float p_half_height, float p_top, float p_bottom, const DebugDraw::Style& p_style)
{
    const vec3 up = normalize(vec3(p_m[1]));
    const vec3 top(p_m * vec4(0.0f, p_half_height, 0.0f, 1.0f));
    const vec3 bottom(p_m * vec4(0.0f, -p_half_height, 0.0f, 1.0f));
    r_draw.circle(top, up, p_top * p_scale, p_style);
    r_draw.circle(bottom, up, p_bottom * p_scale, p_style);
    const vec3 sides[4] = { vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, -1.0f) };
    for (const vec3& d : sides) r_draw.line(vec3(p_m * vec4(d * p_top + vec3(0.0f, p_half_height, 0.0f), 1.0f)), vec3(p_m * vec4(d * p_bottom - vec3(0.0f, p_half_height, 0.0f), 1.0f)), p_style);
}

static void _wedge(DebugDraw& r_draw, const mat4& p_m, const vec3& p_half, const DebugDraw::Style& p_style)
{
    const vec3 h = p_half;
    const vec3 p[6] = { vec3(-h.x, -h.y, -h.z), vec3(h.x, -h.y, -h.z), vec3(-h.x, -h.y, h.z), vec3(h.x, -h.y, h.z), vec3(-h.x, h.y, h.z), vec3(h.x, h.y, h.z) };
    static constexpr int EDGES[9][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 }, { 4, 5 }, { 2, 4 }, { 3, 5 }, { 0, 4 }, { 1, 5 } };
    for (const auto& e : EDGES) r_draw.line(vec3(p_m * vec4(p[e[0]], 1.0f)), vec3(p_m * vec4(p[e[1]], 1.0f)), p_style);
}

void debug_draw_physics_shape(DebugDraw& r_draw, const mat4& p_transform, const PhysicsShape& p_shape, const DebugDraw::Style& p_style)
{
    const mat4& m = p_transform;
    const float scale = _max_scale(m);
    switch (p_shape.type) {
        case PhysicsShapeType::BOX:
            r_draw.box(m, p_shape.half_extents, p_style);
            return;
        case PhysicsShapeType::SPHERE:
            r_draw.sphere(vec3(m[3]), p_shape.radius * scale, p_style);
            return;
        case PhysicsShapeType::CAPSULE:
            r_draw.capsule(vec3(m * vec4(0.0f, -p_shape.half_height, 0.0f, 1.0f)), vec3(m * vec4(0.0f, p_shape.half_height, 0.0f, 1.0f)), p_shape.radius * scale, p_style);
            return;
        case PhysicsShapeType::CYLINDER:
        case PhysicsShapeType::TAPERED_CYLINDER:
            _rings(r_draw, m, scale, p_shape.half_height, p_shape.top_radius, p_shape.radius, p_style);
            return;
        case PhysicsShapeType::TAPERED_CAPSULE:
            r_draw.sphere(vec3(m * vec4(0.0f, p_shape.half_height, 0.0f, 1.0f)), p_shape.top_radius * scale, p_style);
            r_draw.sphere(vec3(m * vec4(0.0f, -p_shape.half_height, 0.0f, 1.0f)), p_shape.radius * scale, p_style);
            _rings(r_draw, m, scale, p_shape.half_height, p_shape.top_radius, p_shape.radius, p_style);
            return;
        case PhysicsShapeType::WEDGE:
            _wedge(r_draw, m, p_shape.half_extents, p_style);
            return;
        default:
            return;
    }
}

void debug_draw_lcollision_shape(DebugDraw& r_draw, const mat4& p_transform, const LCollisionShape& p_shape, const DebugDraw::Style& p_style)
{
    debug_draw_physics_shape(r_draw, p_transform * translate(mat4(1.0f), vec3(p_shape.center)) * mat4_cast(p_shape.orientation()), physics_shape(p_shape), p_style);
}

void debug_draw_collision_shape(DebugDraw& r_draw, const mat4& p_world, const CollisionShapeEntry& p_shape, const DebugDraw::Style& p_style)
{
    debug_draw_physics_shape(r_draw, p_world * collision_shape_matrix(p_shape), physics_shape(p_shape), p_style);
}

}
