#pragma once
#include <core/game/game_session.h>
#include <core/game/physics_system.h>
#include <core/world/world.h>
#include <core/assets/guid.h>
#include <core/base/error.h>

namespace lumen {

struct EditorPlay
{
    World* authored = nullptr;
    World runtime;
    GameSession session;
    PhysicsSystem physics_system;
    Guid world_guid;
    EnvironmentSettings _environment;

    Error initialize();
    void shutdown();

    bool active() const { return session.active(); }
    bool playing() const { return session.playing(); }
    bool editing() const { return session.paused(); }

    void start(World& r_authored, Guid p_world, const Camera& p_camera);
    void stop();
    void toggle();
    void update(float p_dt);

    Entity to_runtime(Entity p_authored) const;
    Entity to_authored(uint32_t p_runtime_index) const;

    void _clone();
    void _sync_entity(Entity p_authored);
    void _sync_environment();
    void _mirror();
};

}
