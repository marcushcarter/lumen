#pragma once
#include <core/game/game_session.h>
#include <core/game/physics_system.h>
#include <core/game/camera_system.h>
#include <core/game/locomotion_system.h>
#include <core/game/movement_system.h>
#include <core/scene/scene.h>
#include <core/assets/guid.h>
#include <core/base/error.h>

namespace lumen {

struct EditorPlay
{
    static constexpr float STEP_DT = 1.0f / 30.0f;
    static constexpr float MIN_SPEED = 0.01f;
    static constexpr float MAX_SPEED = 10.0f;

    Scene* authored = nullptr;
    Scene runtime;
    GameSession session;
    PhysicsSystem physics_system;
    CameraSystem camera_system;
    MovementSystem movement_system;
    LocomotionSystem locomotion_system;
    Guid scene_guid;
    EnvironmentSettings _environment;
    ShadowSettings _shadows;
    PhysicsSettings _physics;
    TerrainSettings _terrain;
    WaterSettings _water;
    bool simulating = false;
    bool step_pending = false;
    bool scaled = false;
    bool scaled_from_pause = false;
    float speed = 1.0f;
    float simulated_dt = 0.0f;

    Error initialize();
    void shutdown();

    bool active() const { return session.active(); }
    bool playing() const { return session.playing(); }
    bool editing() const { return session.paused(); }
    bool game() const { return playing() && !simulating; }

    void start(Scene& r_authored, Guid p_world, const Camera& p_camera);
    void stop();
    void toggle();
    void step();
    void set_scaled(bool p_on);
    void update(float p_dt);

    Entity to_runtime(Entity p_authored) const;
    Entity to_authored(uint32_t p_runtime_index) const;

    void _clone();
    void _sync_entity(Entity p_authored);
    void _sync_environment();
    void _mirror();
};

}
