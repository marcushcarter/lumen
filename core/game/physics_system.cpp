#include <core/game/physics_system.h>
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

Guid PhysicsSystem::_model_mesh(const World& p_world, Entity p_entity) const
{
    if (!renderer) return {};
    const ModelComponent* model = p_world.try_get<ModelComponent>(p_entity);
    if (!model) return {};
    const uint32_t mesh_index = renderer->models.mesh_index(model->model);
    return renderer->geometry.get(mesh_index) ? renderer->geometry.mesh_guids[mesh_index] : Guid{};
}

bool PhysicsSystem::_build(const World& p_world, Entity p_entity, const CollisionComponent& p_collision, const mat4& p_matrix, Body& r_body)
{
    PhysicsBodyDesc desc;
    for (uint32_t i = 0; i < std::min(p_collision.shape_count, COLLISION_MAX_SHAPES); i++) {
        const CollisionShapeEntry& s = p_collision.shapes[i];
        PhysicsShapePart part;
        part.position = s.position;
        part.rotation = quat(radians(s.rotation));
        switch (s.type) {
            case CollisionShapeType::BOX: part.shape = PhysicsShape::box(s.half_extents); break;
            case CollisionShapeType::SPHERE: part.shape = PhysicsShape::sphere(s.radius); break;
            case CollisionShapeType::CAPSULE: part.shape = PhysicsShape::capsule(s.half_height, s.radius); break;
            case CollisionShapeType::MODEL: {
                const Guid mesh = _model_mesh(p_world, p_entity);
                if (!mesh.valid() || !project) continue;
                if (!collision.has(mesh) && !collision.load(mesh, project->content_path(mesh))) continue;
                part.shape = PhysicsShape::mesh_collision(mesh);
                break;
            }
            default: continue;
        }
        desc.parts.push_back(part);
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
    if (physics.initialized()) physics.destroy_all_bodies();
    bodies.clear();
    collision.clear();
}

void PhysicsSystem::_sync(World& r_world, float p_dt)
{
    for (auto& [k, b] : bodies) b.seen = false;

    struct Item {
        Entity entity;
        CollisionComponent collision;
    };
    std::vector<Item> items;
    r_world.view<TransformComponent, CollisionComponent>(Exclude<CharacterComponent>{}, [&](Entity p_entity, const TransformComponent&, const CollisionComponent& p_collision) {
        items.push_back({ p_entity, p_collision });
    });

    for (const Item& item : items) {
        const uint64_t k = key(item.entity);
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
            if (!_build(r_world, item.entity, item.collision, matrix, body)) continue;
            body.seen = true;
            bodies.emplace(k, body);
            optimize_pending = true;
            continue;
        }
        Body& body = it->second;
        body.seen = true;
        if (body.motion == PhysicsMotion::DYNAMIC || matrix == body.source) continue;
        body.source = matrix;
        vec3 position, scale;
        quat rotation;
        decompose(matrix, position, rotation, scale);
        if (body.motion == PhysicsMotion::KINEMATIC) physics.move_kinematic(body.id, position, rotation, std::max(p_dt, PhysicsWorld::FIXED_DT));
        else physics.set_transform(body.id, position, rotation);
    }

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (it->second.seen) { ++it; continue; }
        physics.destroy_body(it->second.id);
        it = bodies.erase(it);
    }
}

void PhysicsSystem::_write_back(World& r_world, uint32_t p_steps)
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

void PhysicsSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !physics.initialized()) return;
    World& world = *r_session.world;
    _sync(world, p_dt);
    if (optimize_pending) {
        physics.optimize_broadphase();
        optimize_pending = false;
    }
    const uint32_t steps = physics.update(p_dt);
    _write_back(world, steps);
}

}
