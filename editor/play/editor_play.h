#pragma once
#include <core/game/game_session.h>
#include <core/game/physics_system.h>
#include <core/game/character_system.h>
#include <core/scene/scene.h>
#include <core/assets/guid.h>
#include <core/base/error.h>

namespace lumen {

struct EditorPlay
{
    static constexpr float STEP_DT = 1.0f / 30.0f;

    Scene* authored = nullptr;
    Scene runtime;
    GameSession session;
    PhysicsSystem physics_system;
    CharacterSystem character_system;
    Guid scene_guid;
    EnvironmentSettings _environment;
    TerrainSettings _terrain;
    ShadowSettings _shadows;
    PhysicsSettings _physics;
    bool step_pending = false;
    float simulated_dt = 0.0f;

    Error initialize();
    void shutdown();

    bool active() const { return session.active(); }
    bool playing() const { return session.playing(); }
    bool editing() const { return session.paused(); }

    void start(Scene& r_authored, Guid p_world, const Camera& p_camera);
    void stop();
    void toggle();
    void step();
    void update(float p_dt);

    Entity to_runtime(Entity p_authored) const;
    Entity to_authored(uint32_t p_runtime_index) const;

    void _clone();
    void _sync_entity(Entity p_authored);
    void _sync_environment();
    void _mirror();
};

}
