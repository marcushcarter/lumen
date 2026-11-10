#pragma once
#include <core/game/game_session.h>
#include <core/game/physics_system.h>
#include <core/world/components.h>
#include <core/world/world.h>
#include <unordered_map>

namespace lumen {

struct CharacterSystem : GameSystem
{
    static constexpr float STICK_TO_FLOOR = 0.5f;
    static constexpr float MOVE_DEADZONE = 0.1f;
    static constexpr float TELEPORT_EPSILON = 1e-3f;

    struct Tracked {
        Entity entity = ENTITY_NULL;
        PhysicsCharacterId id;
        uint64_t hash = 0;
        vec3 written = vec3(0.0f);
        bool seen = false;
    };

    PhysicsSystem* physics = nullptr;
    std::unordered_map<uint64_t, Tracked> characters;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    static uint64_t settings_hash(const CharacterComponent& p_character);
    void _clear();
};

}
