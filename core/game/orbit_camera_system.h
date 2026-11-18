#pragma once
#include <core/game/game_session.h>
#include <core/scene/scene.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct PhysicsSystem;
struct PhysicsWorld;

struct OrbitCameraSystem : GameSystem
{
    static constexpr float START_PITCH = -10.0f;
    static constexpr float FOLLOW = 15.0f;
    static constexpr float FOLLOW_VERTICAL = 8.0f;
    static constexpr float ZOOM = 8.0f;
    static constexpr float FOV_INTERP = 4.0f;
    static constexpr float WALL_GAP = 0.05f;
    static constexpr float MIN_DISTANCE = 0.1f;

    struct Place : GameSystem
    {
        OrbitCameraSystem* owner = nullptr;
        void update(GameSession& r_session, float p_dt) override { owner->place(r_session, p_dt); }
    };

    PhysicsSystem* physics = nullptr;
    Place late;
    Entity target = ENTITY_NULL;
    bool ready = false;
    float yaw = 0.0f;
    float pitch = START_PITCH;
    vec3 pivot = vec3(0.0f);
    float distance = 0.0f;
    float shoulder = 0.0f;
    float base_fov = 0.0f;
    float fov = 0.0f;

    OrbitCameraSystem() { late.owner = this; }

    void start(GameSession& r_session) override;
    void stop(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void place(GameSession& r_session, float p_dt);

    vec3 _pivot_target(Scene& p_world) const;
    const PhysicsWorld* _physics() const;
};

}
