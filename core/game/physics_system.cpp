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
    water.clear();
    physics.water = &PhysicsWorld::NO_WATER;
    physics.buoyancy_forces = false;
    physics.buoyancy_push = false;
    _water_terrain = {};
    if (physics.initialized()) physics.destroy_all_bodies();
    bodies.clear();
    collision.clear();
    _statics.clear();
    _stream_count = 0;
    _stream_pending = false;
}

void PhysicsSystem::_sync(Scene& r_world, float p_dt)
{
    const uint64_t static_key = hash_combine(hash_combine(r_world.static_version, renderer ? renderer->geometry.version : 0), renderer ? renderer->models.version : 0);
    const bool full = !_static_synced || static_key != _static_key;
    for (auto& [k, b] : bodies) b.seen = b.tagged_static && !full;

    std::vector<SyncItem>& items = _items;
    items.clear();
    const auto gather = [&](Entity p_entity, const TransformComponent&, const CollisionComponent& p_collision) { items.push_back({ p_entity, p_collision }); };
    if (full) r_world.view<TransformComponent, CollisionComponent>(Exclude<DisabledTag>{}, gather);
    else r_world.view<TransformComponent, CollisionComponent>(Exclude<StaticTag, DisabledTag>{}, gather);

    bool static_failed = false;
    if (full) _statics.clear();
    for (const SyncItem& item : items) {
        const uint64_t k = key(item.entity);
        const mat4 matrix = r_world.world_matrix(item.entity);
        const uint64_t hash = settings_hash(item.collision, _model_mesh(r_world, item.entity));
        if (full && r_world.has<StaticTag>(item.entity)) {
            StaticItem s{ item.entity, item.collision, matrix, hash };
            if (!_bounds(r_world, item.entity, item.collision, matrix, s.center, s.radius)) static_failed = true;
            _statics.push_back(s);
            continue;
        }
        auto it = bodies.find(k);
        if (it != bodies.end() && (it->second.hash != hash || it->second.tagged_static)) {
            _static_changed(it->second);
            physics.destroy_body(it->second.id);
            bodies.erase(it);
            it = bodies.end();
        }
        if (it == bodies.end()) {
            Body body;
            body.hash = hash;
            if (!_build(r_world, item.entity, item.collision, matrix, body)) continue;
            body.seen = true;
            if (body.motion == PhysicsMotion::DYNAMIC) {
                if (const InitialVelocityComponent* v = r_world.try_get<InitialVelocityComponent>(item.entity)) {
                    physics.set_linear_velocity(body.id, v->linear);
                    physics.set_angular_velocity(body.id, v->angular);
                    r_world.remove<InitialVelocityComponent>(item.entity);
                }
                _update_freeze(body);
            }
            _static_changed(body);
            bodies.emplace(k, body);
            optimize_pending = true;
            floor_dirty = true;
            continue;
        }
        Body& body = it->second;
        body.seen = true;
        if (body.motion == PhysicsMotion::DYNAMIC) {
            _update_freeze(body);
            continue;
        }
        if (matrix == body.source) continue;
        body.source = matrix;
        vec3 position, scale;
        quat rotation;
        decompose(matrix, position, rotation, scale);
        if (body.motion == PhysicsMotion::KINEMATIC) physics.move_kinematic(body.id, position, rotation, std::max(p_dt, PhysicsWorld::FIXED_DT));
        else {
            _static_changed(body);
            physics.set_transform(body.id, position, rotation);
            _static_changed(body);
            floor_dirty = true;
        }
    }

    _stream_statics(r_world, full);

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (it->second.seen) { ++it; continue; }
        _static_changed(it->second);
        physics.destroy_body(it->second.id);
        it = bodies.erase(it);
        floor_dirty = true;
    }

    if (full) {
        _static_key = static_key;
        _static_synced = !static_failed;
        floor_dirty = true;
    }
}

bool PhysicsSystem::_bounds(const Scene& p_world, Entity p_entity, const CollisionComponent& p_collision, const mat4& p_matrix, vec3& r_center, float& r_radius) const
{
    const float scale = std::max(length(vec3(p_matrix[0])), std::max(length(vec3(p_matrix[1])), length(vec3(p_matrix[2]))));
    r_center = vec3(p_matrix[3]);
    r_radius = STREAM_FALLBACK_RADIUS * scale;
    if (p_collision.source == CollisionSource::SHAPES) {
        float reach = 0.0f;
        for (uint32_t i = 0; i < std::min(p_collision.shape_count, COLLISION_MAX_SHAPES); i++) {
            const CollisionShapeEntry& s = p_collision.shapes[i];
            reach = std::max(reach, length(s.position) + std::max({ length(s.half_extents), s.radius + s.half_height, s.top_radius + s.half_height }));
        }
        if (reach > 0.0f) r_radius = reach * scale;
        return true;
    }
    if (!renderer) return true;
    const ModelComponent* model = p_world.try_get<ModelComponent>(p_entity);
    if (!model) return true;
    const LMesh* mesh = renderer->geometry.get(renderer->models.mesh_index(model->model));
    if (!mesh) return false;
    r_center = vec3(p_matrix * vec4(vec3(mesh->bounds_sphere), 1.0f));
    r_radius = mesh->bounds_sphere.w * scale;
    return true;
}

void PhysicsSystem::_update_freeze(Body& r_body)
{
    const float distance = relevance_distance(_view, r_body.curr_position, 0.0f);
    const bool frozen = r_body.frozen ? distance > STREAM_LOAD : distance > STREAM_UNLOAD;
    if (frozen == r_body.frozen) return;
    physics.set_frozen(r_body.id, frozen);
    r_body.frozen = frozen;
}

void PhysicsSystem::_static_changed(const Body& p_body)
{
    if (p_body.motion != PhysicsMotion::STATIC || !water.enabled) return;
    vec3 lo, hi;
    if (physics.body_bounds(p_body.id, lo, hi)) water.static_changed(lo, hi);
}

void PhysicsSystem::_update_water(const Scene& p_world)
{
    const WaterSettings& s = p_world.water;
    if (!s.active()) {
        if (water.enabled) water.clear();
        physics.water = &PhysicsWorld::NO_WATER;
        physics.buoyancy_forces = false;
        physics.buoyancy_push = false;
        return;
    }
    if (_water_terrain != p_world.terrain.terrain) {
        water.clear();
        _water_terrain = p_world.terrain.terrain;
    }
    physics.buoyancy_focus = focus;
    water.prepare(p_world, focus, terrain.valid() ? &terrain.source : nullptr, physics);
    physics.water = &water;
    physics.buoyancy_forces = s.buoyancy;
    physics.buoyancy_drag = s.drag;
    physics.buoyancy_push = s.push_water;
}

void PhysicsSystem::_stream_statics(Scene& r_world, bool p_full)
{
    bool moved = _view.count != _stream_count;
    for (uint32_t i = 0; i < _view.count && !moved; i++) moved = length(_view.points[i] - _stream_points[i]) > STREAM_STEP;
    if (!p_full && !moved && !_stream_pending) return;
    _stream_count = _view.count;
    for (uint32_t i = 0; i < _view.count; i++) _stream_points[i] = _view.points[i];
    _stream_pending = false;

    uint32_t budget = STREAM_BUDGET;
    for (StaticItem& s : _statics) {
        const uint64_t k = key(s.entity);
        const float distance = relevance_distance(_view, s.center, s.radius);
        auto it = bodies.find(k);
        const bool exists = it != bodies.end();
        const bool want = exists ? distance <= STREAM_UNLOAD : distance <= STREAM_LOAD;
        if (exists && (!want || it->second.hash != s.hash || it->second.source != s.matrix || !it->second.tagged_static)) {
            _static_changed(it->second);
            physics.destroy_body(it->second.id);
            bodies.erase(it);
            if (!want) continue;
        } else if (exists) {
            it->second.seen = true;
            continue;
        }
        if (!want || s.failed) continue;
        if (budget == 0) {
            _stream_pending = true;
            continue;
        }
        Body body;
        body.hash = s.hash;
        if (!_build(r_world, s.entity, s.collision, s.matrix, body)) {
            s.failed = true;
            continue;
        }
        body.seen = true;
        body.tagged_static = true;
        _static_changed(body);
        bodies.emplace(k, body);
        budget--;
        if (p_full) optimize_pending = true;
    }
}

void PhysicsSystem::_snapshot_previous()
{
    for (auto& [k, body] : bodies) {
        if (body.motion != PhysicsMotion::DYNAMIC) continue;
        if (!physics.get_transform(body.id, body.prev_position, body.prev_rotation)) {
            body.prev_position = body.curr_position;
            body.prev_rotation = body.curr_rotation;
        }
    }
}

void PhysicsSystem::_write_back(Scene& r_world, uint32_t p_steps)
{
    const float alpha = std::clamp(physics.interpolation_alpha(), 0.0f, 1.0f);
    for (auto& [k, body] : bodies) {
        if (body.motion != PhysicsMotion::DYNAMIC) continue;
        if (p_steps > 0 && !physics.get_transform(body.id, body.curr_position, body.curr_rotation)) continue;
        const vec3 position = mix(body.prev_position, body.curr_position, alpha);
        const quat rotation = slerp(body.prev_rotation, body.curr_rotation, alpha);
        const mat4 matrix = translate(mat4(1.0f), position) * mat4_cast(rotation) * glm::scale(mat4(1.0f), body.scale);
        r_world.set_world_matrix(body.entity, matrix);
        body.source = matrix;
    }
}

void PhysicsSystem::_drift_frozen(float p_dt)
{
    for (auto& [k, b] : bodies) {
        if (!b.frozen || b.motion != PhysicsMotion::DYNAMIC) {
            b.drifting = false;
            continue;
        }
        float surface = 0.0f;
        vec2 flow(0.0f);
        if (!physics.water->sample(b.curr_position.x, b.curr_position.z, surface, flow)) continue;
        if (!b.drifting) {
            b.drifting = true;
            b.drift_offset = std::clamp(b.curr_position.y - surface, DRIFT_OFFSET_MIN, DRIFT_OFFSET_MAX);
        }
        b.curr_position += vec3(flow.x, 0.0f, flow.y) * p_dt;
        b.curr_position.y = surface + b.drift_offset;
        b.prev_position = b.curr_position;
        physics.set_transform(b.id, b.curr_position, b.curr_rotation);
    }
}

void PhysicsSystem::_despawn_fallen(Scene& r_world)
{
    if (!has_floor()) return;
    const float limit = world_floor - DESPAWN_DEPTH;
    std::vector<uint64_t> fallen;
    for (const auto& [k, body] : bodies) {
        if (body.motion == PhysicsMotion::DYNAMIC && !body.frozen && body.curr_position.y < limit) fallen.push_back(k);
    }
    for (const uint64_t k : fallen) {
        const auto it = bodies.find(k);
        const Entity e = it->second.entity;
        physics.destroy_body(it->second.id);
        bodies.erase(it);
        if (r_world.valid(e)) r_world.destroy_subtree(e);
    }
}

void PhysicsSystem::_update_floor()
{
    if (floor_dirty) {
        floor_dirty = false;
        static_floor = FLT_MAX;
        for (const StaticItem& s : _statics) static_floor = std::min(static_floor, s.center.y - s.radius);
        for (const auto& [k, body] : bodies) {
            if (body.motion != PhysicsMotion::STATIC || body.tagged_static) continue;
            vec3 lo, hi;
            if (physics.body_bounds(body.id, lo, hi)) static_floor = std::min(static_floor, lo.y);
        }
    }
    world_floor = terrain.valid() ? std::min(static_floor, terrain.lowest) : static_floor;
}

void PhysicsSystem::_sync_terrain(GameSession& r_session)
{
    const Scene& world = *r_session.world;
    const Guid terrain_guid = world.terrain.terrain;
    const std::filesystem::path terrain_path = terrain_guid.valid() && project ? project->content_path(terrain_guid) : std::filesystem::path();
    _anchors.clear();
    _anchors.push_back(r_session.camera.position);
    world.view<TransformComponent, CharacterComponent>([&](Entity p_entity, const TransformComponent&, const CharacterComponent&) {
        _anchors.push_back(vec3(world.world_matrix(p_entity)[3]));
    });
    terrain.update(physics, terrain_guid, terrain_path, _anchors);
}

void PhysicsSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !physics.initialized()) return;
    Scene& world = *r_session.world;
    const vec3 gravity(0.0f, -world.physics.gravity, 0.0f);
    if (physics.gravity() != gravity) physics.set_gravity(gravity);
    _view = relevance_view(r_session.camera);
    focus = r_session.camera.position;
    bool player = false;
    world.view<TransformComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&) {
        if (!character_is_player(world, p_entity)) return;
        const vec3 p = vec3(world.world_matrix(p_entity)[3]);
        if (!player) focus = p;
        player = true;
        if (_view.count < RelevanceView::MAX_POINTS) _view.add(p);
    });
    _sync(world, p_dt);
    _sync_terrain(r_session);
    _update_floor();
    if (optimize_pending) {
        physics.optimize_broadphase();
        optimize_pending = false;
    }
    _update_water(world);
    if (world.water.active()) _drift_frozen(p_dt);
    const uint32_t steps = physics.advance(p_dt);
    for (uint32_t i = 0; i < steps; i++) {
        if (i + 1 == steps) _snapshot_previous();
        physics.step_fixed();
        if (world.water.simulate) water.step();
    }
    if (world.water.active()) water.publish(r_session.camera.position);
    _write_back(world, steps);
    _despawn_fallen(world);
}

}
