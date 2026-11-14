#pragma once
#include <core/animation/anim_eval.h>
#include <core/assets/llocomotion.h>
#include <core/assets/lskeleton.h>
#include <core/physics/physics_types.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

enum class MovementState : uint8_t { GROUNDED, IN_AIR };
enum class Stance : uint8_t { STANDING, CROUCHING };
enum class Gait : uint8_t { WALKING, RUNNING, SPRINTING };
enum class RotationMode : uint8_t { VELOCITY_DIRECTION, LOOKING_DIRECTION, AIMING };

const char* movement_state_name(MovementState p_state);
const char* stance_name(Stance p_stance);
const char* gait_name(Gait p_gait);
const char* rotation_mode_name(RotationMode p_mode);

enum class AirState : uint8_t { GROUNDED, TAKEOFF, RISE, FALL };

struct AirClip
{
    Guid clip;
    float time = 0.0f;
    float rate = 1.0f;
    bool loop = false;
};

struct CharacterAnim
{
    static constexpr float STANCE_INTERP = 8.0f;
    static constexpr float AIR_INTERP = 10.0f;
    static constexpr float AIR_JUMP_INTERP = 20.0f;
    static constexpr float AIR_LAND_INTERP = 12.0f;
    static constexpr float LAND_BLEND_IN = 0.15f;
    static constexpr float LAND_RUN_FADE = 0.2f;
    static constexpr float LAND_HEAVY_RUN_FADE = 0.35f;
    static constexpr float LAND_RUN_SPEED = 1.0f;
    static constexpr float LAND_BLEND_OUT = 0.4f;
    static constexpr float LAND_MOVING_WEIGHT = 0.4f;
    static constexpr float LAND_MIN_SPEED = 2.0f;
    static constexpr float AIR_TAKEOFF_FADE = 0.08f;
    static constexpr float AIR_RISE_FADE = 0.15f;
    static constexpr float AIR_FALL_FADE = 0.25f;
    static constexpr float AIR_FAST_FADE = 0.35f;
    static constexpr float FALL_GAP = 0.6f;
    static constexpr float FALL_MIN_TIME = 0.25f;
    static constexpr float JUMP_MOVING_SPEED = 1.0f;
    static constexpr float JUMP_WALK_MARGIN = 0.5f;
    static constexpr float JUMP_RATE_MIN = 1.2f;
    static constexpr float JUMP_RATE_MAX = 1.5f;
    static constexpr float JUMP_RATE_SPEED = 6.0f;
    static constexpr float VELOCITY_BLEND_INTERP = 12.0f;
    static constexpr float IDLE_INTERP = 10.0f;
    static constexpr float IDLE_FADE_SPEED = 0.5f;
    static constexpr float HIPS_INTERP = 8.0f;
    static constexpr float HIPS_FORWARD_LIMIT = 70.0f;
    static constexpr float HIPS_BACKWARD_LIMIT = 110.0f;
    static constexpr float HIPS_BUFFER = 5.0f;
    static constexpr float HIPS_CROSS_DISTANCE = 0.06f;
    static constexpr float HIPS_SWITCH_TIMEOUT = 0.5f;
    static constexpr float STOP_SETTLE_FALLBACK = 0.4f;
    static constexpr float STOP_SETTLE_MIN = 0.15f;
    static constexpr float STANDING_MAX_RATE = 3.0f;
    static constexpr float CROUCHING_MAX_RATE = 2.0f;
    static constexpr float DIAGONAL_SCALE = 0.15f;
    static constexpr float PIVOT_DURATION = 0.3f;
    static constexpr float STOP_BLEND_IN = 0.15f;
    static constexpr float STOP_BLEND_OUT = 0.2f;
    static constexpr float DYNAMIC_BLEND = 0.2f;
    static constexpr float DYNAMIC_RATE = 1.5f;
    static constexpr float DYNAMIC_START = 0.8f;
    static constexpr float PIVOT_BLEND_INTERP = 30.0f;
    static constexpr float TURN_MIN_ANGLE = 45.0f;
    static constexpr float TURN_180_THRESHOLD = 130.0f;
    static constexpr float TURN_AIM_RATE_LIMIT = 50.0f;
    static constexpr float TURN_MAX_DELAY = 0.75f;
    static constexpr float TURN_BLEND = 0.2f;
    static constexpr uint32_t TURN_CURVE_SAMPLES = 32;
    static constexpr uint32_t TURN_JOINT_DEPTH = 2;
    static constexpr float ROTATE_THRESHOLD = 50.0f;
    static constexpr float ROTATE_FALLBACK_SPEED = 90.0f;

    float velocity_blend[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float hips_forward = 1.0f;
    bool hips_forward_target = true;
    float hips_pending = 0.0f;
    float idle = 1.0f;
    float idle_time = 0.0f;
    float stand_phase = 0.0f;
    float crouch_phase = 0.0f;
    float play_rate = 0.0f;
    float turn_delay = 0.0f;
    Guid turn_clip;
    float turn_time = -1.0f;
    float turn_duration = 0.0f;
    float turn_scale = 1.0f;
    float turn_weight = 0.0f;
    float turn_applied = 0.0f;
    bool turn_stopping = false;
    std::vector<float> turn_curve;
    Guid rotate_clip;
    float rotate_time = 0.0f;
    float rotate_duration = 0.0f;
    float rotate_speed = 0.0f;
    float rotate_rate = 1.0f;
    float rotate_weight = 0.0f;
    std::vector<float> rotate_curve;
    Stance last_stance = Stance::STANDING;
    bool stance_known = false;
    Guid transition_clip;
    float transition_time = -1.0f;
    float transition_duration = 0.0f;
    float transition_weight = 0.0f;
    float pivot_time = 0.0f;
    Guid stop_clip;
    float stop_time = -1.0f;
    float stop_duration = 0.0f;
    float stop_weight = 0.0f;
    float stop_speed = 0.0f;
    float stop_settle = 0.0f;
    Guid dynamic_clip;
    float dynamic_time = -1.0f;
    float dynamic_duration = 0.0f;
    float dynamic_weight = 0.0f;
    float crouch_weight = 0.0f;
    float air_weight = 0.0f;
    AirState air_state = AirState::GROUNDED;
    AirClip air_current;
    AirClip air_previous;
    float air_fade = 1.0f;
    float air_fade_time = 0.0f;
    float land_time = -1.0f;
    Guid land_clip;
    bool land_heavy = false;
    float land_strength = 1.0f;
    bool started = false;

    std::vector<JointTransform> _stand_pose;
    std::vector<JointTransform> _crouch_pose;
    std::vector<JointTransform> _air_pose;
    std::vector<JointTransform> _air_previous_pose;
    std::vector<JointTransform> _land_pose;
    std::vector<JointTransform> _scratch_pose;
    std::vector<mat4> _turn_globals;
    std::vector<JointTransform> _cycle_poses[CYCLE_DIRECTION_COUNT + 1];
};

struct FootIK
{
    static constexpr float FOOT_HEIGHT = 0.135f;
    static constexpr float TRACE_ABOVE = 0.6f;
    static constexpr float TRACE_BELOW = 0.5f;
    static constexpr float TRACE_RADIUS = 0.06f;
    static constexpr float BALL_DISTANCE = 0.14f;
    static constexpr float MAX_PITCH_STEP = 0.1f;
    static constexpr float MAX_PITCH = 25.0f;
    static constexpr float MAX_RAISE = 0.5f;
    static constexpr float MAX_DROP = 0.45f;
    static constexpr float MAX_PELVIS_DROP = 0.4f;
    static constexpr float FOOT_SPRING = 18.0f;
    static constexpr float PELVIS_SPRING = 10.0f;
    static constexpr float TILT_INTERP = 20.0f;
    static constexpr float ALPHA_INTERP = 15.0f;
    static constexpr float PLANT_HEIGHT = 0.04f;
    static constexpr float UNPLANT_HEIGHT = 0.07f;
    static constexpr float PLANT_VERTICAL_SPEED = 0.3f;
    static constexpr float UNPLANT_VERTICAL_SPEED = 0.5f;
    static constexpr float SWING_DROP_WEIGHT = 0.3f;
    static constexpr float MAX_LOCK_DRIFT = 0.35f;
    static constexpr float LOCK_IN = 25.0f;
    static constexpr float LOCK_OUT = 10.0f;

    Guid skeleton;
    int32_t pelvis = -1;
    int32_t thigh[2] = { -1, -1 };
    int32_t calf[2] = { -1, -1 };
    int32_t foot[2] = { -1, -1 };
    int32_t lean[3] = { -1, -1, -1 };
    int32_t neck = -1;
    int32_t head = -1;
    bool valid = false;
    vec3 model[2] = { vec3(0.0f), vec3(0.0f) };
    float previous_height[2] = { 0.0f, 0.0f };
    float rest_height[2] = { 0.0f, 0.0f };
    bool planted[2] = { false, false };
    bool locked[2] = { false, false };
    vec3 lock_world[2] = { vec3(0.0f), vec3(0.0f) };
    float lock_alpha[2] = { 0.0f, 0.0f };
    float height_offset[2] = { 0.0f, 0.0f };
    float height_velocity[2] = { 0.0f, 0.0f };
    vec3 world_offset[2] = { vec3(0.0f), vec3(0.0f) };
    quat world_tilt[2] = { quat(1.0f, 0.0f, 0.0f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f) };
    float pelvis_offset = 0.0f;
    float pelvis_velocity = 0.0f;
    bool started = false;
    float alpha = 0.0f;
    vec3 offset[2] = { vec3(0.0f), vec3(0.0f) };
    quat tilt[2] = { quat(1.0f, 0.0f, 0.0f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f) };
};

struct Character
{
    Guid asset;
    MovementState movement = MovementState::GROUNDED;
    Stance stance = Stance::STANDING;
    Gait gait = Gait::WALKING;
    Gait desired_gait = Gait::RUNNING;
    RotationMode rotation_mode = RotationMode::LOOKING_DIRECTION;
    RotationMode desired_rotation_mode = RotationMode::LOOKING_DIRECTION;

    vec3 position = vec3(0.0f);
    vec3 spawn_position = vec3(0.0f);
    float spawn_yaw = 0.0f;
    vec3 velocity = vec3(0.0f);
    vec3 acceleration = vec3(0.0f);
    float speed = 0.0f;
    bool moving = false;
    bool has_input = false;
    float input_amount = 0.0f;
    float input_yaw = 0.0f;
    float yaw = 0.0f;
    float target_yaw = 0.0f;
    float velocity_yaw = 0.0f;
    float in_air_yaw = 0.0f;
    float aim_yaw = 0.0f;
    float aim_pitch = 0.0f;
    float aim_yaw_rate = 0.0f;
    bool grounded = true;
    bool jumped = false;
    bool landed = false;
    float turn_delta = 0.0f;
    float step_offset = 0.0f;
    float ungrounded_time = 0.0f;
    float look_yaw = 0.0f;
    float look_pitch = 0.0f;
    float look_weight = 0.0f;
    float lean_lr = 0.0f;
    float lean_fb = 0.0f;
    float model_yaw = 0.0f;
    bool pivot = false;
    bool stop = false;
    int32_t dynamic_foot = -1;
    float dynamic_cooldown = 0.0f;
    float still_time = 0.0f;
    bool was_settled = false;
    float pivot_cooldown = 0.0f;
    float pivot_boost = 0.0f;
    PhysicsCharacterId body;
    Stance body_stance = Stance::STANDING;
    bool teleport = false;
    bool jump_flight = false;
    float ground_gap = -1.0f;
    float land_speed = 0.0f;
    float land_brake_time = 0.0f;
    float land_brake_scale = 1.0f;

    CharacterAnim anim;
    FootIK ik;

    float direction() const;
};

struct CharacterStateComponent
{
    Character character;
};

float character_wrap(float p_degrees);
float character_yaw_of(const vec3& p_vector);
vec3 character_forward(float p_yaw);
vec3 character_right(float p_yaw);

void character_foot_ik(const LLocomotion& p_asset, Character& r_character, const LSkeleton& p_skeleton, JointTransform* r_local, std::vector<mat4>& r_global);
bool character_pose(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose);

}
