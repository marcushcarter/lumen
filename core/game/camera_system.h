#pragma once
#include <core/game/game_session.h>
#include <core/scene/scene.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

struct PhysicsSystem;
struct PhysicsWorld;
struct Renderer;

struct CameraSystem : GameSystem
{
    static constexpr float START_PITCH = -10.0f;
    static constexpr float FOLLOW = 15.0f;
    static constexpr float FOLLOW_VERTICAL = 8.0f;
    static constexpr float ZOOM = 8.0f;
    static constexpr float WALL_GAP = 0.05f;
    static constexpr float MIN_DISTANCE = 0.1f;
    static constexpr float EYE_FOLLOW = 30.0f;
    static constexpr float EYE_SNAP = 1.0f;
    static constexpr float RELEASE_TIME = 3.0f;
    static constexpr float ROLL_CAMERA = 0.6f;
    static constexpr float ACTION_BLEND_IN = 0.45f;
    static constexpr float ACTION_BLEND_OUT = 0.7f;

    struct Place : GameSystem
    {
        CameraSystem* owner = nullptr;
        void update(GameSession& r_session, float p_dt) override { owner->place(r_session, p_dt); }
    };

    PhysicsSystem* physics = nullptr;
    const Renderer* renderer = nullptr;
    Place late;
    Entity target = ENTITY_NULL;
    bool ready = false;
    bool eye_ready = false;
    float yaw = 0.0f;
    float pitch = START_PITCH;
    vec3 pivot = vec3(0.0f);
    vec3 eye = vec3(0.0f);
    float distance = 0.0f;
    float shoulder = 0.0f;
    float blocked = -1.0f;
    float release = 0.0f;
    float release_from = 0.0f;
    bool easing = false;
    bool roll_ready = false;
    float third_person = 0.0f;
    float anchor_weight = 0.0f;
    vec3 anchor = vec3(0.0f);
    quat roll_offset = quat(1.0f, 0.0f, 0.0f, 0.0f);

    CameraSystem() { late.owner = this; }

    void start(GameSession& r_session) override;
    void stop(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void place(GameSession& r_session, float p_dt);

    vec3 _pivot_target(Scene& p_world) const;
    vec3 _eye_target(Scene& p_world) const;
    void _place_orbit(GameSession& r_session, float p_dt);
    void _place_first_person(GameSession& r_session, float p_dt, bool p_follow_head);
    const PhysicsWorld* _physics() const;
    bool _head_rotation(Scene& p_world, quat& r_rotation) const;
};

}
