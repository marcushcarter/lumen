#pragma once
#include <core/game/game_session.h>
#include <core/physics/physics_types.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <unordered_map>

namespace lumen {

struct PhysicsSystem;

struct MovementSystem : GameSystem
{
    static constexpr float WALK_INPUT = 0.5f;
    static constexpr float GROUND_PROBE = 50.0f;
    static constexpr float PIVOT_ANGLE = 120.0f;
    static constexpr float PIVOT_HYSTERESIS = 20.0f;
    static constexpr float PIVOT_SPEED = 0.6f;
    static constexpr float AIR_GRIP = 0.3f;
    static constexpr float JUMP_BUFFER = 0.15f;
    static constexpr float COYOTE_TIME = 0.12f;
    static constexpr float JUMP_LOCK = 0.15f;

    struct Body {
        Entity entity = ENTITY_NULL;
        PhysicsCharacterId id;
        float radius = 0.0f;
        float height = 0.0f;
        float max_slope = 0.0f;
        bool landed = false;
        bool seen = false;
    };

    PhysicsSystem* physics = nullptr;
    std::unordered_map<uint64_t, Body> bodies;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    void _player_input(const GameSession& p_session, const Scene& p_world, Entity p_entity, MovementComponent& r_move) const;
    void _clear();
};

}
