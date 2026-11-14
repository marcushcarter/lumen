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

enum class RagdollJoint : uint8_t { PELVIS, SPINE_LOW, SPINE_HIGH, NECK, HEAD, UPPERARM_L, LOWERARM_L, HAND_L, UPPERARM_R, LOWERARM_R, HAND_R, THIGH_L, CALF_L, FOOT_L, THIGH_R, CALF_R, FOOT_R, COUNT };

inline constexpr uint32_t RAGDOLL_JOINT_COUNT = (uint32_t)RagdollJoint::COUNT;
inline constexpr uint32_t RAGDOLL_PART_COUNT = 16;

enum class RagdollState : uint8_t { OFF, ACTIVE, GETTING_UP };
enum class RagdollPose : uint8_t { HIT, BRACE, BRACE_BACK, PROTECT, RELAX, COUNT };

inline constexpr uint32_t RAGDOLL_POSE_COUNT = (uint32_t)RagdollPose::COUNT;

struct CharacterRagdoll
{
    static constexpr float SETTLE_SPEED = 0.5f;
    static constexpr float SETTLE_TIME = 0.25f;
    static constexpr float MIN_TIME = 0.6f;
    static constexpr float HIT_TIME = 0.2f;
    static constexpr float BRACE_HEIGHT = 0.6f;
    static constexpr float PROTECT_SPEED = 1.0f;
    static constexpr float BRACE_BACK_DOT = -0.5f;
    static constexpr float POSE_INTERP = 8.0f;
    static constexpr float STRENGTH_INTERP = 6.0f;
    static constexpr float MAX_TIME = 8.0f;
    static constexpr float TOGGLE_MIN_TIME = 0.2f;
    static constexpr float GET_UP_BLEND = 0.25f;
    static constexpr float GET_UP_BLEND_OUT = 0.3f;
    static constexpr float SNAP_BLEND = 0.4f;
    static constexpr float MAX_START_SPEED = 25.0f;
    static constexpr float MAX_START_SPIN = 20.0f;
    static constexpr float GROUND_PROBE = 3.0f;
    static constexpr float AIR_PELVIS_HEIGHT = 0.9f;
    static constexpr float MUSCLE_TORQUE = 40.0f;
    static constexpr float MUSCLE_FREQUENCY = 8.0f;
    static constexpr float HIT_RADIUS = 0.9f;
    static constexpr float HIT_STOP = 1.3f;
    static constexpr float SHOVE_SCALE = 1.0f;
    static constexpr float OBSTACLE_PUSH = 0.1f;
    static constexpr float OBSTACLE_MEMORY = 3.0f;
    static constexpr float OBSTACLE_RADIUS = 1.0f;
    static constexpr float MIN_HIT_SCALE = 0.2f;
    static constexpr float STAGGER_MIN = 0.3f;
    static constexpr float STAGGER_RANGE = 0.5f;
    static constexpr float STAGGER_CONTROL = 0.2f;
    static constexpr float GROUND_BOUNCE = 0.3f;
    static constexpr float SWAY_FREQUENCY = 1.6f;
    static constexpr float SWAY_DAMPING = 0.55f;
    static constexpr float SWAY_GAIN = 0.9f;
    static constexpr float SWAY_MAX = 0.45f;
    static constexpr float SWAY_MAX_SPIN = 6.0f;
    static constexpr float SWAY_PIVOT_HEIGHT = 0.95f;
    static constexpr float SWAY_LEVER = 0.8f;
    static constexpr float SWAY_TORSO_BEND = 0.6f;
    static constexpr float SWAY_STEP_ANGLE = 0.12f;
    static constexpr float SWAY_STEP_SPEED = 6.0f;
    static constexpr float MIN_BOUNCE = 0.4f;
    static constexpr float MAX_BOUNCE = 2.5f;
    static constexpr float STUMBLE_REFERENCE_SPEED = 6.0f;
    static constexpr float ATTACK_SHOVE = 0.6f;
    static constexpr float AIR_CATCH_CHANCE = 0.45f;
    static constexpr float AIR_CATCH_RANGE = 4.0f;
    static constexpr float AIR_CATCH_HEIGHT = 1.5f;
    static constexpr float AIR_CATCH_FAR = 0.25f;
    static constexpr float AIR_CATCH_CEILING = 0.5f;
    static constexpr float STAGGER_RELEASE_DOT = 0.3f;
    static constexpr float MIN_FLINCH_SCALE = 0.05f;

    Guid skeleton;
    bool valid = false;
    int32_t joints[RAGDOLL_JOINT_COUNT] = {};
    mat4 rest[RAGDOLL_JOINT_COUNT] = {};
    mat4 current[RAGDOLL_JOINT_COUNT] = {};
    mat4 previous[RAGDOLL_JOINT_COUNT] = {};
    mat4 current_matrix = mat4(1.0f);
    mat4 previous_matrix = mat4(1.0f);
    mat4 matrix = mat4(1.0f);
    float pose_dt = 0.0f;
    bool has_current = false;
    bool has_previous = false;
    std::vector<int8_t> part_of;
    std::vector<JointTransform> snapshot;

    RagdollState state = RagdollState::OFF;
    PhysicsRagdollId body;
    float time = 0.0f;
    float still_time = 0.0f;
    uint64_t step = 0;
    std::vector<vec3> positions;
    std::vector<quat> rotations;
    std::vector<vec3> previous_positions;
    std::vector<quat> previous_rotations;
    std::vector<vec3> velocities;
    vec3 world_scale[RAGDOLL_PART_COUNT] = {};
    vec3 shape_a[RAGDOLL_PART_COUNT] = {};
    vec3 shape_b[RAGDOLL_PART_COUNT] = {};
    float shape_radius[RAGDOLL_PART_COUNT] = {};
    mat4 driven[RAGDOLL_PART_COUNT] = {};
    quat muscle_target[RAGDOLL_PART_COUNT] = {};
    quat poses[RAGDOLL_POSE_COUNT][RAGDOLL_PART_COUNT] = {};
    RagdollPose muscle_pose = RagdollPose::HIT;
    float strength = 0.0f;
    vec3 front = vec3(0.0f, 0.0f, 1.0f);
    float mass[RAGDOLL_PART_COUNT] = {};
    float muscle[RAGDOLL_PART_COUNT] = {};
    vec3 hit_point = vec3(0.0f);
    vec3 hit_direction = vec3(0.0f);
    float hit_speed = 0.0f;
    float hit_push = 0.0f;
    bool hit_pending = false;
    bool hit_soft = false;
    bool land_stumble = false;
    float hit_scale = 1.0f;
    vec3 last_obstacle = vec3(0.0f);
    float obstacle_memory = 0.0f;
    bool face_up = false;
    vec3 pelvis = vec3(0.0f);

    Guid get_up_clip;
    float get_up_time = 0.0f;
    std::vector<JointTransform> _clip_pose;
    std::vector<JointTransform> _blend_pose;

    void bind(const LLocomotion& p_asset, const LSkeleton& p_skeleton);
    void record(const JointTransform* p_local, const std::vector<mat4>& p_global, float p_dt);
    bool build(const LLocomotion& p_asset, float p_model_yaw, const vec3& p_fallback_velocity, PhysicsRagdollDesc& r_desc);
    void hit(const vec3& p_point, const vec3& p_push, float p_speed);
    void update_driven(float p_alpha);
    void update_muscles(const LLocomotion& p_asset, RagdollPose p_pose, float p_dt);
    void update_facing(float p_model_yaw, vec3& r_head);
    void apply_pose(const LSkeleton& p_skeleton, JointTransform* r_local, std::vector<mat4>& r_global);
    void apply_get_up(ClipProvider& r_assets, uint32_t p_joints, float p_dt, JointTransform* r_pose);
};

}
