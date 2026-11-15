#pragma once
#include <core/game/character.h>
#include <core/game/game_session.h>
#include <core/physics/physics_types.h>
#include <core/world/components.h>
#include <core/world/world.h>
#include <vector>

namespace lumen {

struct AnimLibrary;
struct PhysicsSystem;
struct PhysicsWorld;

struct CharacterSystem : GameSystem
{
    static constexpr float SKIN = 0.02f;
    static constexpr float SPAWN_LIFT = 0.05f;
    static constexpr float STEP_SMOOTH = 15.0f;
    static constexpr float STEP_SLOPE_ALLOWANCE = 1.2f;
    static constexpr float STEP_VISUAL_MIN = 0.01f;
    static constexpr float STICK_DOWN = 0.5f;
    static constexpr float MAX_SLOPE_DEGREES = 50.0f;
    static constexpr float WALKABLE_COS = 0.64f;
    static constexpr float RESPAWN_DROP = 100.0f;
    static constexpr float MOVING_SPEED = 1.5f;
    static constexpr float GAIT_MARGIN = 0.1f;
    static constexpr float SPRINT_INPUT = 0.9f;
    static constexpr float SPRINT_ANGLE = 50.0f;
    static constexpr float AIM_LIMIT = 100.0f;
    static constexpr float AIR_GRACE = 0.1f;
    static constexpr float LEAN_INTERP = 4.0f;
    static constexpr float AIR_LEAN_SPEED = 3.5f;
    static constexpr float AIR_LEAN_FALL_SPEED = 5.0f;
    static constexpr float PIVOT_MAX_SPEED = 2.0f;
    static constexpr float PIVOT_MIN_SPEED = 0.5f;
    static constexpr float PIVOT_ANGLE = 135.0f;
    static constexpr float PIVOT_COOLDOWN = 0.5f;
    static constexpr float PIVOT_DURATION = 0.3f;
    static constexpr float STOP_MIN_SPEED = 1.0f;
    static constexpr float DYNAMIC_THRESHOLD = 0.08f;
    static constexpr float DYNAMIC_RETRIGGER = 0.5f;
    static constexpr float DYNAMIC_SETTLE = 0.3f;
    static constexpr float LOOK_INTERP = 10.0f;
    static constexpr float LOOK_FADE_START = 100.0f;
    static constexpr float LOOK_FADE_RANGE = 40.0f;
    static constexpr float PIVOT_ROTATION_BOOST = 2.0f;
    static constexpr float LAND_BRAKE_TIME = 0.5f;
    static constexpr float GROUND_PROBE = 2.0f;
    static constexpr float LAND_DIP_SCALE = 0.12f;
    static constexpr float LAND_DIP_MAX = 1.2f;
    static constexpr float LAND_BRAKE_INPUT = 0.5f;
    static constexpr float LAND_BRAKE_IDLE = 3.0f;

    static constexpr float IMPACT_PROBE_RADIUS = 0.12f;
    static constexpr float IMPACT_REACH = 0.15f;
    static constexpr float IMPACT_MIN_SPEED = 0.5f;
    static constexpr float HEAD_PROBE_DROP = 0.2f;
    static constexpr float BODY_PROBE_HEIGHT = 0.45f;
    static constexpr float CHEST_PROBE_HEIGHT = 0.6f;
    static constexpr float IMPACT_FACING = -0.3f;
    static constexpr float WALL_MAX_NORMAL_Y = 0.6f;
    static constexpr float AIR_FLINCH_SPEED = 1.5f;
    static constexpr float LEG_PROBE_HEIGHT = 0.25f;
    static constexpr float WALL_FLINCH_SPEED = 2.0f;
    static constexpr float STUMBLE_FACING = 0.7f;
    static constexpr float AIR_RISE_SPEED = 1.0f;
    static constexpr float TEST_HIT_LIGHT = 2.5f;
    static constexpr float TEST_HIT_HEAVY = 9.0f;
    static constexpr float TEST_HIT_HEIGHT = 0.7f;
    static constexpr float TEST_HIT_LOW_HEIGHT = 0.2f;

    static constexpr float CAMERA_DISTANCE = 3.0f;
    static constexpr float CAMERA_AIM_DISTANCE = 1.6f;
    static constexpr float CAMERA_SHOULDER = 0.35f;
    static constexpr float CAMERA_AIM_SHOULDER = 0.55f;
    static constexpr float CAMERA_PIVOT_HEIGHT = 0.9f;
    static constexpr float CAMERA_RADIUS = 0.2f;
    static constexpr float CAMERA_FOLLOW = 15.0f;
    static constexpr float CAMERA_FOLLOW_VERTICAL = 8.0f;
    static constexpr float CAMERA_SPRINT_FOV = 5.0f;
    static constexpr float CAMERA_FOV_INTERP = 4.0f;
    static constexpr float CAMERA_ZOOM = 8.0f;
    static constexpr float CAMERA_PITCH_MIN = -70.0f;
    static constexpr float CAMERA_PITCH_MAX = 60.0f;
    static constexpr float CAMERA_START_PITCH = -10.0f;

    PhysicsSystem* physics = nullptr;
    AnimLibrary* anims = nullptr;

    bool debug = true;
    Entity player = ENTITY_NULL;
    bool camera_ready = false;
    float camera_yaw = 0.0f;
    float camera_pitch = CAMERA_START_PITCH;
    vec3 camera_pivot = vec3(0.0f);
    float camera_distance = CAMERA_DISTANCE;
    float camera_shoulder = CAMERA_SHOULDER;
    float camera_base_fov = 0.0f;
    float camera_fov = 0.0f;

    std::vector<Entity> _entities;
    mutable uint32_t _seed = 0x9E3779B9u;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    static void respawn(Character& r_character);
    const PhysicsWorld* _physics() const;
    static bool _low(const Character& p_character);
    static float _height(const Character& p_character, const LocomotionComponent& p_body);
    static float _stance_height(const Character& p_character, const LocomotionComponent& p_body);
    bool _cast(float p_height, float p_radius, const vec3& p_position, const vec3& p_direction, float p_distance, PhysicsHit& r_hit) const;
    PhysicsWorld* _physics_mut() const;
    void _sync_shape(Character& r_character, const LocomotionComponent& p_body) const;
    void _move(Character& r_character, const LocomotionComponent& p_body, float p_dt) const;
    static void _smooth_rotation(Character& r_character, float p_target, float p_target_speed, float p_actor_speed, float p_dt);
    static void _rotate(Character& r_character, const CharacterStance& p_stance, float p_dt);
    void _simulate(Character& r_character, const LLocomotion& p_asset, const LocomotionComponent& p_body, const GameInput& p_input, float p_aim_yaw, float p_aim_pitch, float p_dt) const;
    bool _ground_at(const vec3& p_position, float p_top, PhysicsHit& r_hit) const;
    void _update_foot_ik(Character& r_character, const LLocomotion& p_asset, float p_model_yaw, float p_dt) const;
    bool _detect_impact(Character& r_character, const LLocomotion& p_asset, const LocomotionComponent& p_body, float p_dt) const;
    void _apply_hit(Character& r_character, const LLocomotion& p_asset) const;
    bool _air_catch(const Character& p_character, float p_limit) const;
    float _random() const;
    void _stumble(Character& r_character) const;
    static void _update_sway(Character& r_character, float p_dt);
    static void _impact_body(Character& r_character, const vec3& p_impulse);
    void _start_ragdoll(Character& r_character, const LLocomotion& p_asset) const;
    void _update_ragdoll(Character& r_character, const LLocomotion& p_asset, bool p_toggle, float p_dt) const;
    void _end_ragdoll(Character& r_character, const LLocomotion& p_asset) const;
    void _draw_ragdoll(const Character& p_character) const;
    void _update_camera(GameSession& r_session, const Character& p_character, const LocomotionComponent& p_body, float p_dt);
};

}
