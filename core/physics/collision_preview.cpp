#include <core/physics/collision_preview.h>
#include <core/assets/lmesh.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace lumen {

mat4 collision_shape_matrix(const CollisionShapeEntry& p_shape)
{
    return translate(mat4(1.0f), p_shape.position) * mat4_cast(quat(radians(p_shape.rotation)));
}

const CollisionPreview::Entry* CollisionPreview::get(Guid p_mesh, const std::filesystem::path& p_bin)
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

    std::vector<uint64_t> keys;
    keys.reserve(e.data.indices.size());
    for (size_t i = 0; i + 2 < e.data.indices.size(); i += 3) {
        for (int k = 0; k < 3; k++) {
            const uint32_t a = e.data.indices[i + k];
            const uint32_t b = e.data.indices[i + (k + 1) % 3];
            keys.push_back(((uint64_t)std::min(a, b) << 32) | std::max(a, b));
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    e.edges.reserve(keys.size() * 2);
    for (uint64_t k : keys) {
        e.edges.push_back((uint32_t)(k >> 32));
        e.edges.push_back((uint32_t)k);
    }
    return &e;
}

uint32_t CollisionPreview::color(const CollisionComponent& p_collision)
{
    if (p_collision.trigger) return DebugDraw::YELLOW;
    switch (p_collision.motion) {
        case PhysicsMotion::DYNAMIC: return DebugDraw::ORANGE;
        case PhysicsMotion::KINEMATIC: return DebugDraw::CYAN;
        default: return DebugDraw::GREEN;
    }
}

static float _max_scale(const mat4& p_m)
{
    return std::max(length(vec3(p_m[0])), std::max(length(vec3(p_m[1])), length(vec3(p_m[2]))));
}

void CollisionPreview::draw_model(DebugDraw& r_draw, const mat4& p_transform, const Entry& p_entry, const DebugDraw::Style& p_style)
{
    const LCollision& c = p_entry.data;
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
        default:
            for (size_t i = 0; i + 1 < p_entry.edges.size(); i += 2) {
                r_draw.line(vec3(p_transform * vec4(c.vertices[p_entry.edges[i]], 1.0f)), vec3(p_transform * vec4(c.vertices[p_entry.edges[i + 1]], 1.0f)), p_style);
            }
            return;
    }
}

void CollisionPreview::draw_shape(DebugDraw& r_draw, const mat4& p_world, const CollisionShapeEntry& p_shape, const Entry* p_model, const DebugDraw::Style& p_style)
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
            if (p_model) draw_model(r_draw, m, *p_model, p_style);
            return;
        default:
            return;
    }
}

void CollisionPreview::draw_world(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer, const Project& p_project)
{
    p_world.view<TransformComponent, CharacterComponent>(Exclude<EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent&, const CharacterComponent& p_character) {
        const vec3 feet = vec3(p_world.world_matrix(p_entity)[3]);
        const float radius = std::max(p_character.radius, 0.001f);
        const float top = std::max(p_character.height - radius, radius);
        r_draw.capsule(feet + vec3(0.0f, radius, 0.0f), feet + vec3(0.0f, top, 0.0f), radius, DebugDraw::Style{ .color = DebugDraw::BLUE, .category = DebugCategory::COLLISION });
    });
    p_world.view<TransformComponent, CollisionComponent>(Exclude<EditorHiddenTag, CharacterComponent>{}, [&](Entity p_entity, const TransformComponent&, const CollisionComponent& p_collision) {
        const mat4 world = p_world.world_matrix(p_entity);
        const DebugDraw::Style style{ .color = color(p_collision), .category = DebugCategory::COLLISION };
        const Entry* model = nullptr;
        for (uint32_t i = 0; i < std::min(p_collision.shape_count, COLLISION_MAX_SHAPES); i++) {
            const CollisionShapeEntry& shape = p_collision.shapes[i];
            if (shape.type == CollisionShapeType::MODEL && !model) {
                const ModelComponent* m = p_world.try_get<ModelComponent>(p_entity);
                const uint32_t mesh_index = m ? p_renderer.models.mesh_index(m->model) : UINT32_MAX;
                if (p_renderer.geometry.get(mesh_index)) {
                    const Guid mesh = p_renderer.geometry.mesh_guids[mesh_index];
                    model = get(mesh, p_project.content_path(mesh));
                }
            }
            draw_shape(r_draw, world, shape, model, style);
        }
    });
}

}
