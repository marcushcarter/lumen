#include <core/game/physics_system.h>
#include <core/physics/collision_preview.h>
#include <core/base/console.h>
#include <core/base/hash.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <vector>

namespace lumen {

static uint64_t _hash_float(uint64_t p_hash, float p_value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &p_value, sizeof(bits));
    return hash_combine(p_hash, bits);
}

static uint64_t _hash_vec3(uint64_t p_hash, const vec3& p_value)
{
    return _hash_float(_hash_float(_hash_float(p_hash, p_value.x), p_value.y), p_value.z);
}

uint64_t PhysicsSystem::settings_hash(const CollisionComponent& p_collision, Guid p_model)
{
    uint64_t h = hash_combine((uint64_t)p_collision.motion, p_collision.trigger ? 1u : 0u);
    h = hash_combine(h, (uint64_t)p_collision.source);
    h = _hash_float(h, p_collision.mass);
    h = _hash_float(h, p_collision.friction);
    h = _hash_float(h, p_collision.restitution);
    h = hash_combine(h, p_collision.shape_count);
    h = hash_combine(h, p_model.value);
    for (uint32_t i = 0; i < std::min(p_collision.shape_count, COLLISION_MAX_SHAPES); i++) {
        const CollisionShapeEntry& s = p_collision.shapes[i];
        h = hash_combine(h, (uint64_t)s.type);
        h = _hash_vec3(h, s.position);
        h = _hash_vec3(h, s.rotation);
        h = _hash_vec3(h, s.half_extents);
        h = _hash_float(h, s.radius);
        h = _hash_float(h, s.half_height);
        h = _hash_float(h, s.top_radius);
    }
    return h;
}

void PhysicsSystem::decompose(const mat4& p_world, vec3& r_position, quat& r_rotation, vec3& r_scale)
{
    r_position = vec3(p_world[3]);
    r_scale = vec3(length(vec3(p_world[0])), length(vec3(p_world[1])), length(vec3(p_world[2])));
    const mat3 basis(vec3(p_world[0]) / std::max(r_scale.x, 1e-6f), vec3(p_world[1]) / std::max(r_scale.y, 1e-6f), vec3(p_world[2]) / std::max(r_scale.z, 1e-6f));
    r_rotation = normalize(quat_cast(basis));
}

Guid PhysicsSystem::_model_mesh(const Scene& p_world, Entity p_entity) const
{
    if (!renderer) return {};
    const ModelComponent* model = p_world.try_get<ModelComponent>(p_entity);
    if (!model) return {};
    const uint32_t mesh_index = renderer->models.mesh_index(model->model);
    return renderer->geometry.get(mesh_index) ? renderer->geometry.mesh_guids[mesh_index] : Guid{};
}

bool PhysicsSystem::_build(const Scene& p_world, Entity p_entity, const CollisionComponent& p_collision, const mat4& p_matrix, Body& r_body)
{
    PhysicsBodyDesc desc;
    if (p_collision.source == CollisionSource::MESH) {
        const Guid mesh = _model_mesh(p_world, p_entity);
        if (mesh.valid() && project && (collision.has(mesh) || collision.load(mesh, project->content_path(mesh)))) {
            PhysicsShapePart part;
            part.shape = PhysicsShape::mesh_collision(mesh);
            desc.parts.push_back(part);
        }
    } else {
        for (uint32_t i = 0; i < std::min(p_collision.shape_count, COLLISION_MAX_SHAPES); i++) {
            const CollisionShapeEntry& s = p_collision.shapes[i];
            PhysicsShapePart part;
            part.position = s.position;
            part.rotation = quat(radians(s.rotation));
            part.shape = physics_shape(s);
            desc.parts.push_back(part);
        }
    }
    if (desc.parts.empty()) return false;

    decompose(p_matrix, desc.position, desc.rotation, desc.scale);
    desc.motion = p_collision.motion;
    desc.layer = p_collision.trigger ? PhysicsLayer::TRIGGER : (p_collision.motion == PhysicsMotion::DYNAMIC ? PhysicsLayer::DYNAMIC : (p_collision.motion == PhysicsMotion::KINEMATIC ? PhysicsLayer::KINEMATIC : PhysicsLayer::STATIC));
    desc.sensor = p_collision.trigger;
    desc.mass = p_collision.motion == PhysicsMotion::DYNAMIC ? p_collision.mass : 0.0f;
    desc.friction = p_collision.friction;
    desc.restitution = p_collision.restitution;
    desc.user_data = key(p_entity);

    r_body.id = physics.create_body(desc);
    if (!r_body.id.valid()) return false;
    r_body.entity = p_entity;
    r_body.motion = p_collision.motion;
    r_body.source = p_matrix;
    r_body.scale = desc.scale;
    r_body.prev_position = r_body.curr_position = desc.position;
    r_body.prev_rotation = r_body.curr_rotation = desc.rotation;
    return true;
}

void PhysicsSystem::start(GameSession& r_session)
{
    (void)r_session;
    _clear();
    if (!physics.initialized() && physics.initialize() != Error::OK) {
        log_write("Physics: couldn't create the physics world.");
        return;
    }
    physics.collision_cache = &collision;
    physics.accumulator = 0.0f;
    optimize_pending = true;
}

void PhysicsSystem::stop(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void PhysicsSystem::_clear()
{
    floor_dirty = true;
    _static_synced = false;
    static_floor = FLT_MAX;
    world_floor = FLT_MAX;
    terrain.clear(physics);
    if (physics.initialized()) physics.destroy_all_bodies();
    bodies.clear();
    collision.clear();
}

void PhysicsSystem::_sync(Scene& r_world, float p_dt)
{
    const uint64_t static_key = hash_combine(hash_combine(r_world.static_version, renderer ? renderer->geometry.version : 0), renderer ? renderer->models.version : 0);
    const bool full = !_static_synced || static_key != _static_key;
    for (auto& [k, b] : bodies) b.seen = !full && b.tagged_static;

    std::vector<SyncItem>& items = _items;
    items.clear();
    const auto gather = [&](Entity p_entity, const TransformComponent&, const CollisionComponent& p_collision) { items.push_back({ p_entity, p_collision }); };
    if (full) r_world.view<TransformComponent, CollisionComponent>(Exclude<DisabledTag, LocomotionComponent>{}, gather);
    else r_world.view<TransformComponent, CollisionComponent>(Exclude<StaticTag, DisabledTag, LocomotionComponent>{}, gather);

    bool static_failed = false;
    for (const SyncItem& item : items) {
        const uint64_t k = key(item.entity);
        const bool tagged = full && r_world.has<StaticTag>(item.entity);
        const mat4 matrix = r_world.world_matrix(item.entity);
        const uint64_t hash = settings_hash(item.collision, _model_mesh(r_world, item.entity));
        auto it = bodies.find(k);
        if (it != bodies.end() && it->second.hash != hash) {
            physics.destroy_body(it->second.id);
            bodies.erase(it);
            it = bodies.end();
        }
        if (it == bodies.end()) {
            Body body;
            body.hash = hash;
            if (!_build(r_world, item.entity, item.collision, matrix, body)) {
                static_failed |= tagged;
                continue;
            }
            body.seen = true;
            body.tagged_static = tagged;
            bodies.emplace(k, body);
            optimize_pending = true;
            floor_dirty = true;
            continue;
        }
        Body& body = it->second;
        body.seen = true;
        if (full) body.tagged_static = tagged;
        if (body.motion == PhysicsMotion::DYNAMIC || matrix == body.source) continue;
        body.source = matrix;
        vec3 position, scale;
        quat rotation;
        decompose(matrix, position, rotation, scale);
        if (body.motion == PhysicsMotion::KINEMATIC) physics.move_kinematic(body.id, position, rotation, std::max(p_dt, PhysicsWorld::FIXED_DT));
        else {
            physics.set_transform(body.id, position, rotation);
            floor_dirty = true;
        }
    }

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (it->second.seen) { ++it; continue; }
        physics.destroy_body(it->second.id);
        it = bodies.erase(it);
        floor_dirty = true;
    }

    if (full) {
        _static_key = static_key;
        _static_synced = !static_failed;
    }
}

void PhysicsSystem::_write_back(Scene& r_world, uint32_t p_steps)
{
    const float alpha = std::clamp(physics.interpolation_alpha(), 0.0f, 1.0f);
    for (auto& [k, body] : bodies) {
        if (body.motion != PhysicsMotion::DYNAMIC) continue;
        if (p_steps > 0) {
            body.prev_position = body.curr_position;
            body.prev_rotation = body.curr_rotation;
            if (!physics.get_transform(body.id, body.curr_position, body.curr_rotation)) continue;
        }
        const vec3 position = mix(body.prev_position, body.curr_position, alpha);
        const quat rotation = slerp(body.prev_rotation, body.curr_rotation, alpha);
        const mat4 matrix = translate(mat4(1.0f), position) * mat4_cast(rotation) * glm::scale(mat4(1.0f), body.scale);
        r_world.set_world_matrix(body.entity, matrix);
        body.source = matrix;
    }
}

void PhysicsSystem::_update_floor()
{
    if (floor_dirty) {
        floor_dirty = false;
        static_floor = FLT_MAX;
        for (const auto& [k, body] : bodies) {
            if (body.motion != PhysicsMotion::STATIC) continue;
            vec3 lo, hi;
            if (physics.body_bounds(body.id, lo, hi)) static_floor = std::min(static_floor, lo.y);
        }
    }
    world_floor = terrain.source.valid() ? std::min(static_floor, terrain.lowest) : static_floor;
}

void PhysicsSystem::_sync_terrain(GameSession& r_session)
{
    const Scene& world = *r_session.world;
    const Guid guid = world.terrain.terrain;
    const std::filesystem::path path = guid.valid() && project ? project->content_path(guid) : std::filesystem::path();
    _anchors.clear();
    _anchors.push_back(r_session.camera.position);
    world.view<TransformComponent, LocomotionComponent>([&](Entity p_entity, const TransformComponent&, const LocomotionComponent&) {
        _anchors.push_back(vec3(world.world_matrix(p_entity)[3]));
    });
    terrain.update(physics, guid, path, _anchors);
}

void PhysicsSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !physics.initialized()) return;
    Scene& world = *r_session.world;
    const vec3 gravity(0.0f, -world.physics.gravity, 0.0f);
    if (physics.gravity() != gravity) physics.set_gravity(gravity);
    _sync(world, p_dt);
    _sync_terrain(r_session);
    _update_floor();
    if (optimize_pending) {
        physics.optimize_broadphase();
        optimize_pending = false;
    }
    const uint32_t steps = physics.update(p_dt);
    _write_back(world, steps);
}

}
