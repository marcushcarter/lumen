#include <core/game/character_ragdoll.h>
#include <core/animation/clip_player.h>
#include <core/animation/pose_blend.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static const vec3 UP = vec3(0.0f, 1.0f, 0.0f);

struct RagdollPartDef
{
    RagdollJoint joint;
    int8_t parent;
    float mass;
    float radius;
    PhysicsJoint type;
    float swing_normal;
    float swing_plane;
    float twist;
    float flex;
    float bend;
};

using enum RagdollJoint;

static const RagdollPartDef PARTS[RAGDOLL_PART_COUNT] = {
    { PELVIS, -1, 0.14f, 0.12f, PhysicsJoint::SWING_TWIST, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { SPINE_LOW, 0, 0.12f, 0.11f, PhysicsJoint::SWING_TWIST, 20.0f, 30.0f, 10.0f, 0.0f, 0.0f },
    { SPINE_HIGH, 1, 0.16f, 0.12f, PhysicsJoint::SWING_TWIST, 20.0f, 30.0f, 15.0f, 0.0f, 0.0f },
    { HEAD, 2, 0.08f, 0.1f, PhysicsJoint::SWING_TWIST, 40.0f, 45.0f, 45.0f, 0.0f, 0.0f },
    { UPPERARM_L, 2, 0.03f, 0.055f, PhysicsJoint::SWING_TWIST, 70.0f, 110.0f, 60.0f, 0.0f, 0.0f },
    { LOWERARM_L, 4, 0.02f, 0.045f, PhysicsJoint::HINGE, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { HAND_L, 5, 0.006f, 0.04f, PhysicsJoint::SWING_TWIST, 40.0f, 50.0f, 15.0f, 0.0f, 0.0f },
    { UPPERARM_R, 2, 0.03f, 0.055f, PhysicsJoint::SWING_TWIST, 70.0f, 110.0f, 60.0f, 0.0f, 0.0f },
    { LOWERARM_R, 7, 0.02f, 0.045f, PhysicsJoint::HINGE, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { HAND_R, 8, 0.006f, 0.04f, PhysicsJoint::SWING_TWIST, 40.0f, 50.0f, 15.0f, 0.0f, 0.0f },
    { THIGH_L, 0, 0.1f, 0.075f, PhysicsJoint::SWING_TWIST, 30.0f, 70.0f, 20.0f, 25.0f, 0.0f },
    { CALF_L, 10, 0.045f, 0.055f, PhysicsJoint::HINGE, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f },
    { FOOT_L, 11, 0.015f, 0.045f, PhysicsJoint::SWING_TWIST, 15.0f, 35.0f, 10.0f, 0.0f, 0.0f },
    { THIGH_R, 0, 0.1f, 0.075f, PhysicsJoint::SWING_TWIST, 30.0f, 70.0f, 20.0f, 25.0f, 0.0f },
    { CALF_R, 13, 0.045f, 0.055f, PhysicsJoint::HINGE, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f },
    { FOOT_R, 14, 0.015f, 0.045f, PhysicsJoint::SWING_TWIST, 15.0f, 35.0f, 10.0f, 0.0f, 0.0f },
};

static const float POSE_FLEX[RAGDOLL_POSE_COUNT][RAGDOLL_PART_COUNT] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 10, 10, 15, 60, 30, 0, 60, 30, 0, 25, -30, 0, 25, -30, 0 },
    { 0, 5, 5, 25, -45, 20, 0, -45, 20, 0, 20, -25, 0, 20, -25, 0 },
    { 0, 25, 25, 35, 100, 110, 20, 100, 110, 20, 80, -100, 10, 80, -100, 10 },
    { 0, 10, 10, 15, 40, 50, 10, 40, 50, 10, 35, -45, 5, 35, -45, 5 },
};

static const float POSE_STRENGTH[RAGDOLL_POSE_COUNT] = { 1.0f, 0.8f, 0.8f, 0.5f, 0.2f };

static constexpr float HEAD_LENGTH = 0.16f;
static constexpr float HAND_LENGTH = 0.1f;
static constexpr float FOOT_FORWARD = 0.15f;
static constexpr float FOOT_DOWN = 0.07f;
static constexpr float CHEST_WIDTH = 0.55f;
static constexpr float KNEE_MIN = -5.0f;
static constexpr float KNEE_MAX = 140.0f;
static constexpr float ELBOW_MAX = 145.0f;
static constexpr float JOINT_FRICTION = 1.5f;

static quat _rotation(const mat4& p_m)
{
    const mat3 r(normalize(vec3(p_m[0])), normalize(vec3(p_m[1])), normalize(vec3(p_m[2])));
    return normalize(quat_cast(r));
}

static vec3 _scale(const mat4& p_m)
{
    return vec3(length(vec3(p_m[0])), length(vec3(p_m[1])), length(vec3(p_m[2])));
}

static vec3 _dir(const vec3& p_from, const vec3& p_to, const vec3& p_fallback)
{
    const vec3 d = p_to - p_from;
    const float len = length(d);
    return len > 1.0e-4f ? d / len : p_fallback;
}

static vec3 _perpendicular(const vec3& p_axis, const vec3& p_forward)
{
    vec3 p = cross(p_axis, p_forward);
    if (length(p) < 0.1f) p = cross(p_axis, UP);
    if (length(p) < 0.1f) p = cross(p_axis, vec3(1.0f, 0.0f, 0.0f));
    return normalize(p);
}

static quat _from_to(const vec3& p_from, const vec3& p_to)
{
    const float d = dot(p_from, p_to);
    if (d > 0.9999f) return quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f) return angleAxis(glm::pi<float>(), _perpendicular(p_from, vec3(0.0f, 0.0f, 1.0f)));
    return normalize(quat(1.0f + d, cross(p_from, p_to)));
}

static vec3 _model_forward(float p_model_yaw)
{
    return angleAxis(glm::radians(-p_model_yaw), UP) * vec3(0.0f, 0.0f, 1.0f);
}

struct RagdollFrame
{
    vec3 position[RAGDOLL_JOINT_COUNT];
    quat rotation[RAGDOLL_JOINT_COUNT];
};

static void _segment(uint32_t p_part, const RagdollFrame& p_frame, const vec3 p_foot[2], vec3& r_a, vec3& r_b)
{
    const vec3* p = p_frame.position;
    const RagdollJoint j = PARTS[p_part].joint;
    const auto at = [&](RagdollJoint p_joint) { return p[(uint32_t)p_joint]; };
    switch (j) {
        case PELVIS: r_a = at(THIGH_L); r_b = at(THIGH_R); return;
        case SPINE_LOW: r_a = at(SPINE_LOW); r_b = at(SPINE_HIGH); return;
        case SPINE_HIGH: r_a = mix(at(SPINE_HIGH), at(UPPERARM_L), CHEST_WIDTH); r_b = mix(at(SPINE_HIGH), at(UPPERARM_R), CHEST_WIDTH); return;
        case HEAD: r_a = at(HEAD); r_b = at(HEAD) + _dir(at(NECK), at(HEAD), UP) * HEAD_LENGTH; return;
        case UPPERARM_L: r_a = at(UPPERARM_L); r_b = at(LOWERARM_L); return;
        case LOWERARM_L: r_a = at(LOWERARM_L); r_b = at(HAND_L); return;
        case HAND_L: r_a = at(HAND_L); r_b = at(HAND_L) + _dir(at(LOWERARM_L), at(HAND_L), -UP) * HAND_LENGTH; return;
        case UPPERARM_R: r_a = at(UPPERARM_R); r_b = at(LOWERARM_R); return;
        case LOWERARM_R: r_a = at(LOWERARM_R); r_b = at(HAND_R); return;
        case HAND_R: r_a = at(HAND_R); r_b = at(HAND_R) + _dir(at(LOWERARM_R), at(HAND_R), -UP) * HAND_LENGTH; return;
        case THIGH_L: r_a = at(THIGH_L); r_b = at(CALF_L); return;
        case CALF_L: r_a = at(CALF_L); r_b = at(FOOT_L); return;
        case FOOT_L: r_a = at(FOOT_L); r_b = at(FOOT_L) + p_foot[0]; return;
        case THIGH_R: r_a = at(THIGH_R); r_b = at(CALF_R); return;
        case CALF_R: r_a = at(CALF_R); r_b = at(FOOT_R); return;
        case FOOT_R: r_a = at(FOOT_R); r_b = at(FOOT_R) + p_foot[1]; return;
        default: r_a = r_b = at(j); return;
    }
}

static vec3 _axis(uint32_t p_part, const RagdollFrame& p_frame, const vec3 p_foot[2])
{
    const vec3* p = p_frame.position;
    const auto at = [&](RagdollJoint p_joint) { return p[(uint32_t)p_joint]; };
    switch (PARTS[p_part].joint) {
        case SPINE_LOW: return _dir(at(SPINE_LOW), at(SPINE_HIGH), UP);
        case SPINE_HIGH: return _dir(at(SPINE_HIGH), at(NECK), UP);
        case HEAD: return _dir(at(NECK), at(HEAD), UP);
        case UPPERARM_L: return _dir(at(UPPERARM_L), at(LOWERARM_L), -UP);
        case LOWERARM_L: return _dir(at(LOWERARM_L), at(HAND_L), -UP);
        case HAND_L: return _dir(at(LOWERARM_L), at(HAND_L), -UP);
        case UPPERARM_R: return _dir(at(UPPERARM_R), at(LOWERARM_R), -UP);
        case LOWERARM_R: return _dir(at(LOWERARM_R), at(HAND_R), -UP);
        case HAND_R: return _dir(at(LOWERARM_R), at(HAND_R), -UP);
        case THIGH_L: return _dir(at(THIGH_L), at(CALF_L), -UP);
        case CALF_L: return _dir(at(CALF_L), at(FOOT_L), -UP);
        case FOOT_L: return _dir(vec3(0.0f), p_foot[0], vec3(0.0f, 0.0f, 1.0f));
        case THIGH_R: return _dir(at(THIGH_R), at(CALF_R), -UP);
        case CALF_R: return _dir(at(CALF_R), at(FOOT_R), -UP);
        case FOOT_R: return _dir(vec3(0.0f), p_foot[1], vec3(0.0f, 0.0f, 1.0f));
        default: return UP;
    }
}

static bool _find_joints(const LLocomotion& p_names, const LSkeleton& p_skeleton, int32_t r_joints[RAGDOLL_JOINT_COUNT])
{
    const std::string* names[RAGDOLL_JOINT_COUNT] = {
        &p_names.pelvis_bone, &p_names.lean_bones[0], &p_names.lean_bones[2], &p_names.neck_bone, &p_names.head_bone,
        &p_names.upperarm_bones[0], &p_names.lowerarm_bones[0], &p_names.hand_bones[0],
        &p_names.upperarm_bones[1], &p_names.lowerarm_bones[1], &p_names.hand_bones[1],
        &p_names.thigh_bones[0], &p_names.calf_bones[0], &p_names.foot_bones[0],
        &p_names.thigh_bones[1], &p_names.calf_bones[1], &p_names.foot_bones[1],
    };
    bool found = true;
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) {
        r_joints[j] = p_skeleton.find(*names[j]);
        if (r_joints[j] < 0) found = false;
    }
    return found;
}

bool RagdollBody::bind(const LLocomotion& p_names, const LSkeleton& p_skeleton)
{
    valid = _find_joints(p_names, p_skeleton, joints);
    if (!valid) return false;
    std::vector<mat4> globals(p_skeleton.joint_count());
    p_skeleton.globals(p_skeleton.rest.data(), globals.data());
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) rest_rotation[j] = _rotation(globals[joints[j]]);
    return true;
}

int32_t RagdollBody::part_joint(uint32_t p_part) const
{
    return p_part < RAGDOLL_PART_COUNT ? joints[(uint32_t)PARTS[p_part].joint] : SKELETON_NO_JOINT;
}

void RagdollBody::capsules(const std::vector<mat4>& p_global, const mat4& p_model, float p_model_yaw, vec3* r_a, vec3* r_b, float* r_radius) const
{
    RagdollFrame frame;
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) {
        const mat4 w = p_model * p_global[joints[j]];
        frame.position[j] = vec3(w[3]);
        frame.rotation[j] = _rotation(w);
    }
    const vec3 foot_rest = _model_forward(p_model_yaw) * FOOT_FORWARD - UP * FOOT_DOWN;
    vec3 foot[2];
    for (int s = 0; s < 2; s++) {
        const uint32_t j = (uint32_t)(s == 0 ? FOOT_L : FOOT_R);
        foot[s] = normalize(frame.rotation[j] * (inverse(rest_rotation[j]) * foot_rest)) * length(foot_rest);
    }
    for (uint32_t i = 0; i < RAGDOLL_PART_COUNT; i++) {
        vec3 a, b;
        _segment(i, frame, foot, a, b);
        const float radius = PARTS[i].radius;
        const float half = std::max(length(b - a) * 0.5f - radius * 0.5f, 0.01f);
        const vec3 dir = _dir(a, b, UP);
        const vec3 mid = (a + b) * 0.5f;
        r_a[i] = mid - dir * half;
        r_b[i] = mid + dir * half;
        r_radius[i] = radius;
    }
}

void CharacterRagdoll::bind(const LLocomotion& p_asset, const LSkeleton& p_skeleton)
{
    if (skeleton == p_skeleton.guid && part_of.size() == p_skeleton.joint_count()) return;
    skeleton = p_skeleton.guid;
    has_current = has_previous = false;
    valid = _find_joints(p_asset, p_skeleton, joints);
    part_of.assign(p_skeleton.joint_count(), -1);
    if (!valid) return;
    for (uint32_t i = 0; i < RAGDOLL_PART_COUNT; i++) part_of[joints[(uint32_t)PARTS[i].joint]] = (int8_t)i;
    std::vector<mat4> globals(p_skeleton.joint_count());
    p_skeleton.globals(p_skeleton.rest.data(), globals.data());
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) rest[j] = globals[joints[j]];
}

void CharacterRagdoll::record(const JointTransform* p_local, const std::vector<mat4>& p_global, float p_dt)
{
    if (!valid || p_global.size() != part_of.size()) return;
    if (state != RagdollState::GETTING_UP) snapshot.assign(p_local, p_local + p_global.size());
    has_previous = has_current;
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) {
        previous[j] = current[j];
        current[j] = p_global[joints[j]];
    }
    previous_matrix = current_matrix;
    current_matrix = matrix;
    pose_dt = p_dt;
    has_current = true;
}

bool CharacterRagdoll::build(const LLocomotion& p_asset, float p_model_yaw, const vec3& p_fallback_velocity, PhysicsRagdollDesc& r_desc)
{
    if (!valid || !has_current) return false;
    const vec3 forward = _model_forward(p_model_yaw);

    RagdollFrame rest_frame, world_frame, before_frame;
    vec3 joint_scale[RAGDOLL_JOINT_COUNT];
    for (uint32_t j = 0; j < RAGDOLL_JOINT_COUNT; j++) {
        rest_frame.position[j] = vec3(rest[j][3]);
        rest_frame.rotation[j] = _rotation(rest[j]);
        const mat4 w = current_matrix * current[j];
        world_frame.position[j] = vec3(w[3]);
        world_frame.rotation[j] = _rotation(w);
        joint_scale[j] = _scale(w);
        const mat4 b = has_previous ? previous_matrix * previous[j] : w;
        before_frame.position[j] = vec3(b[3]);
        before_frame.rotation[j] = _rotation(b);
    }

    const auto to_world = [&](RagdollJoint p_joint, const vec3& p_rest) {
        const uint32_t j = (uint32_t)p_joint;
        return normalize(world_frame.rotation[j] * (inverse(rest_frame.rotation[j]) * p_rest));
    };

    const vec3 foot_rest = forward * FOOT_FORWARD - UP * FOOT_DOWN;
    const vec3 rest_foot[2] = { foot_rest, foot_rest };
    const vec3 world_foot[2] = { to_world(FOOT_L, foot_rest) * length(foot_rest), to_world(FOOT_R, foot_rest) * length(foot_rest) };

    const bool moving = has_previous && pose_dt > 1.0e-4f;
    float total = 0.0f;
    for (const RagdollPartDef& def : PARTS) total += def.mass;

    const uint32_t parts = RAGDOLL_PART_COUNT;
    r_desc = PhysicsRagdollDesc{};
    r_desc.parts.resize(parts);
    for (uint32_t i = 0; i < parts; i++) {
        const RagdollPartDef& def = PARTS[i];
        const uint32_t j = (uint32_t)def.joint;
        PhysicsRagdollPart& part = r_desc.parts[i];
        part.parent = def.parent;
        part.position = world_frame.position[j];
        part.rotation = world_frame.rotation[j];
        part.mass = p_asset.ragdoll_mass * def.mass / total;
        mass[i] = part.mass;
        muscle_target[i] = def.parent >= 0 ? normalize(inverse(world_frame.rotation[(uint32_t)PARTS[def.parent].joint]) * world_frame.rotation[j]) : quat(1.0f, 0.0f, 0.0f, 0.0f);
        world_scale[i] = joint_scale[j];

        vec3 a, b;
        _segment(i, world_frame, world_foot, a, b);
        const quat inv = inverse(part.rotation);
        const vec3 la = inv * (a - part.position);
        const vec3 lb = inv * (b - part.position);
        const float len = length(lb - la);
        const vec3 dir = _dir(la, lb, UP);
        part.shape.shape = PhysicsShape::capsule(std::max(len * 0.5f - def.radius * 0.5f, 0.01f), def.radius);
        part.shape.position = (la + lb) * 0.5f;
        part.shape.rotation = _from_to(UP, dir);
        shape_a[i] = part.shape.position - dir * part.shape.shape.half_height;
        shape_b[i] = part.shape.position + dir * part.shape.shape.half_height;
        shape_radius[i] = def.radius;

        if (moving) {
            vec3 v = (world_frame.position[j] - before_frame.position[j]) / pose_dt;
            if (length(v) > MAX_START_SPEED) v = normalize(v) * MAX_START_SPEED;
            part.linear_velocity = v;
            quat delta = normalize(world_frame.rotation[j] * inverse(before_frame.rotation[j]));
            if (delta.w < 0.0f) delta = -delta;
            const float angle = 2.0f * std::acos(std::clamp(delta.w, -1.0f, 1.0f));
            const float s = std::sqrt(std::max(1.0f - delta.w * delta.w, 0.0f));
            vec3 w = s > 1.0e-4f ? vec3(delta.x, delta.y, delta.z) / s * (angle / pose_dt) : vec3(0.0f);
            if (length(w) > MAX_START_SPIN) w = normalize(w) * MAX_START_SPIN;
            part.angular_velocity = w;
        } else {
            part.linear_velocity = p_fallback_velocity;
        }
        if (hit_speed > 0.0f) {
            float near = std::clamp(1.0f - length(world_frame.position[j] - hit_point) / HIT_RADIUS, 0.0f, 1.0f);
            const float into = dot(part.linear_velocity, hit_direction);
            const float stop = std::max(into, 0.0f) + hit_push;
            part.linear_velocity -= hit_direction * stop * near * HIT_STOP;
        }

        for (uint32_t p = 0; p < RAGDOLL_POSE_COUNT; p++) poses[p][i] = quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (def.parent < 0) continue;
        const RagdollJoint parent = PARTS[def.parent].joint;
        const vec3 twist = _axis(i, rest_frame, rest_foot);
        const quat rest_parent = rest_frame.rotation[(uint32_t)parent];
        const quat rest_child = rest_frame.rotation[j];
        const vec3 side = cross(twist, forward);
        poses[(uint32_t)RagdollPose::HIT][i] = muscle_target[i];
        for (uint32_t p = 1; p < RAGDOLL_POSE_COUNT; p++) {
            const quat bend = length(side) > 0.1f ? angleAxis(glm::radians(POSE_FLEX[p][i]), normalize(side)) : quat(1.0f, 0.0f, 0.0f, 0.0f);
            poses[p][i] = normalize(inverse(rest_parent) * bend * rest_child);
        }
        const vec3 plane = _perpendicular(twist, forward);
        part.joint_position = world_frame.position[j];
        part.joint_friction = part.mass * JOINT_FRICTION;
        if (def.type == PhysicsJoint::HINGE) {
            vec3 hinge = cross(twist, forward * def.bend);
            hinge = length(hinge) > 0.1f ? normalize(hinge) : plane;
            part.joint = PhysicsJoint::HINGE;
            part.parent_axis = to_world(parent, hinge);
            part.parent_normal = to_world(parent, twist);
            part.child_axis = to_world(def.joint, hinge);
            part.child_normal = to_world(def.joint, twist);
            part.twist_min = glm::radians(KNEE_MIN);
            part.twist_max = glm::radians(def.bend > 0.0f ? ELBOW_MAX : KNEE_MAX);
        } else {
            quat flex(1.0f, 0.0f, 0.0f, 0.0f);
            if (def.flex != 0.0f && length(side) > 0.1f) flex = angleAxis(glm::radians(def.flex), normalize(side));
            part.joint = PhysicsJoint::SWING_TWIST;
            part.parent_axis = to_world(parent, flex * twist);
            part.parent_normal = to_world(parent, flex * plane);
            part.child_axis = to_world(def.joint, twist);
            part.child_normal = to_world(def.joint, plane);
            part.swing_normal = glm::radians(def.swing_normal);
            part.swing_plane = glm::radians(def.swing_plane);
            part.twist_min = -glm::radians(def.twist);
            part.twist_max = glm::radians(def.twist);
        }
    }
    return true;
}

void CharacterRagdoll::hit(const vec3& p_point, const vec3& p_push, float p_speed)
{
    const float len = length(p_push);
    if (len < 1.0e-4f || p_speed <= 0.0f) return;
    hit_pending = true;
    hit_point = p_point;
    hit_direction = -p_push / len;
    hit_speed = p_speed;
    hit_push = p_speed * ATTACK_SHOVE;
    hit_scale = 1.0f;
    hit_soft = false;
}

void CharacterRagdoll::update_driven(float p_alpha)
{
    const uint32_t count = std::min((uint32_t)positions.size(), RAGDOLL_PART_COUNT);
    if (count == 0) return;
    const bool blend = previous_positions.size() == count;
    const float alpha = std::clamp(p_alpha, 0.0f, 1.0f);
    const mat4 inv = inverse(matrix);
    for (uint32_t i = 0; i < count; i++) {
        const vec3 position = blend ? mix(previous_positions[i], positions[i], alpha) : positions[i];
        const quat rotation = blend ? slerp(previous_rotations[i], rotations[i], alpha) : rotations[i];
        if (i == 0) pelvis = position;
        driven[i] = inv * (translate(mat4(1.0f), position) * mat4_cast(rotation) * scale(mat4(1.0f), world_scale[i]));
    }
}

void CharacterRagdoll::update_muscles(const LLocomotion& p_asset, RagdollPose p_pose, float p_dt)
{
    const uint32_t index = std::min((uint32_t)p_pose, RAGDOLL_POSE_COUNT - 1);
    muscle_pose = (RagdollPose)index;
    const float target = p_asset.muscle_strength * POSE_STRENGTH[index];
    const float pose_mix = p_dt > 0.0f ? 1.0f - std::exp(-POSE_INTERP * p_dt) : 1.0f;
    const float strength_blend = p_dt > 0.0f ? 1.0f - std::exp(-STRENGTH_INTERP * p_dt) : 1.0f;
    strength += (target - strength) * strength_blend;
    for (uint32_t i = 0; i < RAGDOLL_PART_COUNT; i++) {
        muscle_target[i] = normalize(slerp(muscle_target[i], poses[index][i], pose_mix));
        muscle[i] = strength * MUSCLE_TORQUE * mass[i];
    }
}

void CharacterRagdoll::update_facing(float p_model_yaw, vec3& r_head)
{
    if (rotations.empty()) return;
    const quat pelvis_rotation = rotations[0];
    const quat rest_inverse = inverse(_rotation(rest[(uint32_t)PELVIS]));
    front = pelvis_rotation * (rest_inverse * _model_forward(p_model_yaw));
    r_head = pelvis_rotation * (rest_inverse * UP);
    face_up = front.y > 0.0f;
}

void CharacterRagdoll::apply_pose(const LSkeleton& p_skeleton, JointTransform* r_local, std::vector<mat4>& r_global)
{
    const uint32_t n = p_skeleton.joint_count();
    r_global.resize(n);
    if (part_of.size() != n) {
        p_skeleton.globals(r_local, r_global.data());
        return;
    }
    for (uint32_t j = 0; j < n; j++) {
        const int32_t parent = p_skeleton.parents[j];
        const int8_t part = part_of[j];
        if (part >= 0) {
            r_global[j] = driven[part];
            const mat4 local = (parent >= 0 ? inverse(r_global[parent]) : mat4(1.0f)) * r_global[j];
            r_local[j].translation = vec3(local[3]);
            r_local[j].rotation = _rotation(local);
            r_local[j].scale = _scale(local);
        } else {
            r_global[j] = (parent >= 0 ? r_global[parent] : mat4(1.0f)) * r_local[j].matrix();
        }
    }
}

void CharacterRagdoll::apply_get_up(ClipProvider& r_assets, uint32_t p_joints, float p_dt, JointTransform* r_pose)
{
    const BoundClip clip = r_assets.clip(get_up_clip);
    const bool has_clip = clip.valid() && clip.clip->duration > 0.0f;
    const float duration = has_clip ? clip.clip->duration : SNAP_BLEND;
    const float t = get_up_time;
    if (has_clip) {
        _clip_pose.assign(r_pose, r_pose + p_joints);
        clip_sample(clip, std::min(t, duration), _clip_pose.data());
        const float weight = std::clamp((duration - t) / GET_UP_BLEND_OUT, 0.0f, 1.0f);
        const PoseInput inputs[2] = { { r_pose, 1.0f - weight }, { _clip_pose.data(), weight } };
        _blend_pose.resize(p_joints);
        pose_blend(inputs, 2, p_joints, _blend_pose.data());
        std::copy(_blend_pose.begin(), _blend_pose.end(), r_pose);
    }
    if (snapshot.size() == p_joints) {
        const float in = std::clamp(t / (has_clip ? GET_UP_BLEND : SNAP_BLEND), 0.0f, 1.0f);
        if (in < 1.0f) {
            const PoseInput inputs[2] = { { snapshot.data(), 1.0f - in }, { r_pose, in } };
            _blend_pose.resize(p_joints);
            pose_blend(inputs, 2, p_joints, _blend_pose.data());
            std::copy(_blend_pose.begin(), _blend_pose.end(), r_pose);
        }
    }
    get_up_time += p_dt;
    if (get_up_time >= duration) state = RagdollState::OFF;
}

}
