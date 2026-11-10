#pragma once
#include <core/game/game_session.h>
#include <core/physics/collision_cache.h>
#include <core/physics/physics_world.h>
#include <core/world/components.h>
#include <core/world/world.h>
#include <unordered_map>

namespace lumen {

struct Renderer;
struct Project;

struct PhysicsSystem : GameSystem
{
    struct Body {
        Entity entity = ENTITY_NULL;
        PhysicsBodyId id;
        PhysicsMotion motion = PhysicsMotion::STATIC;
        uint64_t hash = 0;
        mat4 source = mat4(1.0f);
        vec3 scale = vec3(1.0f);
        vec3 prev_position = vec3(0.0f);
        vec3 curr_position = vec3(0.0f);
        quat prev_rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        quat curr_rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        bool seen = false;
    };

    const Renderer* renderer = nullptr;
    const Project* project = nullptr;
    PhysicsWorld physics;
    CollisionCache collision;
    std::unordered_map<uint64_t, Body> bodies;
    bool optimize_pending = false;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    static uint64_t key(Entity p_entity) { return ((uint64_t)p_entity.index << 32) | p_entity.generation; }
    static uint64_t settings_hash(const CollisionComponent& p_collision, Guid p_model);
    static void decompose(const mat4& p_world, vec3& r_position, quat& r_rotation, vec3& r_scale);

    Guid _model_mesh(const World& p_world, Entity p_entity) const;
    bool _build(const World& p_world, Entity p_entity, const CollisionComponent& p_collision, const mat4& p_matrix, Body& r_body);
    void _sync(World& r_world, float p_dt);
    void _write_back(World& r_world, uint32_t p_steps);
    void _clear();
};

}
