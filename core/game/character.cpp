#include <core/game/character.h>
#include <core/animation/clip_player.h>
#include <core/animation/pose_blend.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

const char* movement_state_name(MovementState p_state)
{
    return p_state == MovementState::IN_AIR ? "In Air" : "Grounded";
}

const char* stance_name(Stance p_stance)
{
    return p_stance == Stance::CROUCHING ? "Crouching" : "Standing";
}

const char* gait_name(Gait p_gait)
{
    switch (p_gait) {
        case Gait::WALKING: return "Walking";
        case Gait::RUNNING: return "Running";
        case Gait::SPRINTING: return "Sprinting";
    }
    return "Walking";
}

const char* rotation_mode_name(RotationMode p_mode)
{
    switch (p_mode) {
        case RotationMode::VELOCITY_DIRECTION: return "Velocity Direction";
        case RotationMode::LOOKING_DIRECTION: return "Looking Direction";
        case RotationMode::AIMING: return "Aiming";
    }
    return "Looking Direction";
}

float character_wrap(float p_degrees)
{
    float d = std::fmod(p_degrees + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

float character_yaw_of(const vec3& p_vector)
{
    return glm::degrees(std::atan2(p_vector.x, p_vector.z));
}

vec3 character_forward(float p_yaw)
{
    const float a = glm::radians(p_yaw);
    return vec3(std::sin(a), 0.0f, std::cos(a));
}

vec3 character_right(float p_yaw)
{
    const float a = glm::radians(p_yaw);
    return vec3(-std::cos(a), 0.0f, std::sin(a));
}

float Character::direction() const
{
    const vec3 flat(velocity.x, 0.0f, velocity.z);
    if (length(flat) < 0.01f) return 0.0f;
    return glm::degrees(std::atan2(dot(flat, character_right(yaw)), dot(flat, character_forward(yaw))));
}

static float _interp(float p_current, float p_target, float p_dt, float p_speed)
{
    return p_current + (p_target - p_current) * std::clamp(p_dt * p_speed, 0.0f, 1.0f);
}

static void _mix(JointTransform* r_pose, const JointTransform* p_other, float p_weight, uint32_t p_joints)
{
    if (p_weight <= BLEND_MIN_WEIGHT) return;
    const PoseInput inputs[2] = { { r_pose, 1.0f - p_weight }, { p_other, p_weight } };
    pose_blend(inputs, 2, p_joints, r_pose);
}

static float _mapped_speed(const CharacterStance& p_stance, float p_speed)
{
    if (p_speed <= p_stance.walk_speed) return p_stance.walk_speed > 0.0f ? p_speed / p_stance.walk_speed : 1.0f;
    if (p_speed <= p_stance.run_speed) return 1.0f + (p_speed - p_stance.walk_speed) / std::max(p_stance.run_speed - p_stance.walk_speed, 0.01f);
    return 2.0f + std::min((p_speed - p_stance.run_speed) / std::max(p_stance.sprint_speed - p_stance.run_speed, 0.01f), 1.0f);
}

static void _update_directions(CharacterAnim& r_anim, float p_direction, bool p_moving, float p_speed, float p_dt, bool p_snap, bool p_feet_crossing)
{
    const float a = glm::radians(p_direction);
    const float x = std::cos(a);
    const float y = std::sin(a);
    const float sum = std::fabs(x) + std::fabs(y);
    const float target[4] = { std::max(x, 0.0f) / sum, std::max(-x, 0.0f) / sum, std::max(-y, 0.0f) / sum, std::max(y, 0.0f) / sum };
    if (p_moving) {
        const float angle = std::fabs(p_direction);
        bool desired = r_anim.hips_forward_target;
        if (r_anim.hips_forward_target && angle > CharacterAnim::HIPS_BACKWARD_LIMIT + CharacterAnim::HIPS_BUFFER) desired = false;
        else if (!r_anim.hips_forward_target && angle < CharacterAnim::HIPS_FORWARD_LIMIT - CharacterAnim::HIPS_BUFFER) desired = true;
        if (desired != r_anim.hips_forward_target) {
            r_anim.hips_pending += p_dt;
            if (p_snap || p_feet_crossing || r_anim.hips_pending >= CharacterAnim::HIPS_SWITCH_TIMEOUT) {
                r_anim.hips_forward_target = desired;
                r_anim.hips_pending = 0.0f;
            }
        } else {
            r_anim.hips_pending = 0.0f;
        }
        const float speed = r_anim.pivot_time > 0.0f ? CharacterAnim::PIVOT_BLEND_INTERP : CharacterAnim::VELOCITY_BLEND_INTERP;
        for (int i = 0; i < 4; i++) r_anim.velocity_blend[i] = p_snap ? target[i] : _interp(r_anim.velocity_blend[i], target[i], p_dt, speed);
    }
    const float hips = r_anim.hips_forward_target ? 1.0f : 0.0f;
    r_anim.hips_forward = p_snap ? hips : _interp(r_anim.hips_forward, hips, p_dt, CharacterAnim::HIPS_INTERP);
    const float idle = std::clamp(1.0f - p_speed / CharacterAnim::IDLE_FADE_SPEED, 0.0f, 1.0f);
    r_anim.idle = p_snap ? idle : _interp(r_anim.idle, idle, p_dt, CharacterAnim::IDLE_INTERP);
}

static bool _stance_pose(const CharacterStance& p_stance, CharacterAnim& r_anim, float& r_phase, float p_max_rate, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float p_speed, float p_dt, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    const float* vb = r_anim.velocity_blend;
    const float h = r_anim.hips_forward;
    float weights[CYCLE_DIRECTION_COUNT] = { vb[0], vb[1], vb[2] * h, vb[2] * (1.0f - h), vb[3] * h, vb[3] * (1.0f - h) };
    const LBlendSpace* spaces[CYCLE_DIRECTION_COUNT];
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) spaces[d] = r_assets.blend_space(p_stance.cycles[d]);
    const uint32_t pairs[2][2] = { { (uint32_t)CycleDirection::LEFT_FORWARD, (uint32_t)CycleDirection::LEFT_BACKWARD }, { (uint32_t)CycleDirection::RIGHT_FORWARD, (uint32_t)CycleDirection::RIGHT_BACKWARD } };
    for (const auto& pair : pairs) {
        if (!spaces[pair[1]]) { weights[pair[0]] += weights[pair[1]]; weights[pair[1]] = 0.0f; }
        else if (!spaces[pair[0]]) { weights[pair[1]] += weights[pair[0]]; weights[pair[0]] = 0.0f; }
    }

    const float value = _mapped_speed(p_stance, p_speed);
    float total = 0.0f;
    float duration = 0.0f;
    float animated = 0.0f;
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) {
        if (weights[d] <= BLEND_MIN_WEIGHT || !spaces[d]) continue;
        const AnimEvaluator::BlendInfo info = r_evaluator.blend_info(r_assets, *spaces[d], value);
        if (!info.valid) {
            weights[d] = 0.0f;
            continue;
        }
        total += weights[d];
        duration += weights[d] * info.duration;
        animated += weights[d] * info.speed;
    }
    if (total > 0.0f) {
        const float speed = animated / total;
        r_anim.play_rate = speed > 0.0f ? std::clamp(p_speed / speed, 0.0f, p_max_rate) : 1.0f;
        r_phase += p_dt * r_anim.play_rate / (duration / total);
        r_phase -= std::floor(r_phase);
    }

    std::vector<PoseInput> inputs;
    const BoundClip idle = r_assets.clip(p_stance.idle);
    const bool has_idle = idle.valid() && idle.clip->duration > 0.0f;
    const float moving = total > 0.0f ? (has_idle ? 1.0f - r_anim.idle : 1.0f) : 0.0f;
    if (has_idle && (r_anim.idle > BLEND_MIN_WEIGHT || total <= 0.0f)) {
        std::vector<JointTransform>& pose = r_anim._cycle_poses[CYCLE_DIRECTION_COUNT];
        pose = p_skeleton.rest;
        clip_sample(*idle.clip, *idle.map, std::fmod(r_anim.idle_time, idle.clip->duration), pose.data());
        inputs.push_back({ pose.data(), total > 0.0f ? r_anim.idle : 1.0f });
    }
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT && total > 0.0f; d++) {
        const float w = moving * weights[d] / total;
        if (w <= BLEND_MIN_WEIGHT) continue;
        std::vector<JointTransform>& pose = r_anim._cycle_poses[d];
        pose = p_skeleton.rest;
        if (r_evaluator.blend_sample(r_assets, p_skeleton, *spaces[d], value, r_phase, pose.data())) inputs.push_back({ pose.data(), w });
    }
    if (inputs.empty()) return false;
    pose_blend(inputs.data(), (uint32_t)inputs.size(), joints, r_pose);
    return true;
}

static void _rotate_roots(const LSkeleton& p_skeleton, const quat& p_rotation, JointTransform* r_pose)
{
    for (uint32_t j = 0; j < p_skeleton.joint_count(); j++) {
        if (p_skeleton.parents[j] >= 0) continue;
        r_pose[j].rotation = p_rotation * r_pose[j].rotation;
        r_pose[j].translation = p_rotation * r_pose[j].translation;
    }
}

static float _flat_yaw(const vec3& p_axis)
{
    return glm::degrees(std::atan2(p_axis.x, p_axis.z));
}

static bool _build_yaw_curve(const LSkeleton& p_skeleton, const BoundClip& p_clip, CharacterAnim& r_anim, std::vector<float>& r_curve)
{
    const uint32_t joints = p_skeleton.joint_count();
    std::vector<uint32_t> depth(joints, 0);
    std::vector<uint32_t> candidates;
    for (uint32_t j = 0; j < joints; j++) {
        const int32_t parent = p_skeleton.parents[j];
        depth[j] = parent >= 0 ? depth[parent] + 1 : 0;
        if (depth[j] <= CharacterAnim::TURN_JOINT_DEPTH) candidates.push_back(j);
    }
    if (candidates.empty()) return false;

    const uint32_t samples = CharacterAnim::TURN_CURVE_SAMPLES;
    std::vector<float> yaws(candidates.size() * samples, 0.0f);
    std::vector<float> last(candidates.size(), 0.0f);
    std::vector<uint8_t> use_z(candidates.size(), 1);
    r_anim._turn_globals.resize(joints);
    for (uint32_t s = 0; s < samples; s++) {
        r_anim._scratch_pose = p_skeleton.rest;
        clip_sample(*p_clip.clip, *p_clip.map, p_clip.clip->duration * (float)s / (float)(samples - 1), r_anim._scratch_pose.data());
        p_skeleton.globals(r_anim._scratch_pose.data(), r_anim._turn_globals.data());
        for (size_t c = 0; c < candidates.size(); c++) {
            const mat4& m = r_anim._turn_globals[candidates[c]];
            const vec3 x = normalize(vec3(m[0]));
            const vec3 z = normalize(vec3(m[2]));
            if (s == 0) use_z[c] = std::fabs(z.y) <= std::fabs(x.y) ? 1 : 0;
            const float yaw = _flat_yaw(use_z[c] ? z : x);
            float& out = yaws[c * samples + s];
            out = s == 0 ? yaw : yaws[c * samples + s - 1] + character_wrap(yaw - last[c]);
            last[c] = yaw;
        }
    }

    size_t best = 0;
    float best_total = 0.0f;
    for (size_t c = 0; c < candidates.size(); c++) {
        const float total = yaws[c * samples + samples - 1] - yaws[c * samples];
        if (std::fabs(total) > std::fabs(best_total)) {
            best_total = total;
            best = c;
        }
    }
    r_curve.resize(samples);
    for (uint32_t s = 0; s < samples; s++) r_curve[s] = yaws[best * samples + s] - yaws[best * samples];
    return true;
}

static float _curve_at(const std::vector<float>& p_curve, float p_duration, float p_time)
{
    if (p_curve.size() < 2 || p_duration <= 0.0f) return 0.0f;
    const float f = std::clamp(p_time / p_duration, 0.0f, 1.0f) * (float)(p_curve.size() - 1);
    const uint32_t i = std::min((uint32_t)f, (uint32_t)p_curve.size() - 2);
    return p_curve[i] + (p_curve[i + 1] - p_curve[i]) * (f - (float)i);
}

static bool _still(const Character& p_character)
{
    return p_character.movement == MovementState::GROUNDED && !p_character.has_input && p_character.speed < 0.01f;
}

static void _layer(const LSkeleton& p_skeleton, const BoundClip& p_clip, float p_time, float p_counter_yaw, float p_weight, CharacterAnim& r_anim, JointTransform* r_pose, bool p_posed)
{
    r_anim._scratch_pose = p_skeleton.rest;
    clip_sample(*p_clip.clip, *p_clip.map, p_time, r_anim._scratch_pose.data());
    if (p_counter_yaw != 0.0f) _rotate_roots(p_skeleton, angleAxis(glm::radians(-p_counter_yaw), vec3(0.0f, 1.0f, 0.0f)), r_anim._scratch_pose.data());
    if (p_posed) _mix(r_pose, r_anim._scratch_pose.data(), p_weight, p_skeleton.joint_count());
    else std::copy(r_anim._scratch_pose.begin(), r_anim._scratch_pose.end(), r_pose);
}

static bool _stance_transition(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose, bool p_posed)
{
    CharacterAnim& a = r_character.anim;
    if (!a.stance_known) {
        a.stance_known = true;
        a.last_stance = r_character.stance;
    }
    if (r_character.stance != a.last_stance) {
        a.last_stance = r_character.stance;
        const Guid guid = r_character.stance == Stance::CROUCHING ? p_asset.stand_to_crouch : p_asset.crouch_to_stand;
        const BoundClip clip = r_assets.clip(guid);
        if (_still(r_character) && clip.valid() && clip.clip->duration > 0.0f) {
            a.transition_clip = guid;
            a.transition_time = 0.0f;
            a.transition_duration = clip.clip->duration;
            a.transition_weight = 0.0f;
            a.turn_stopping = true;
        }
    }
    if (a.transition_time < 0.0f) return false;
    const BoundClip clip = r_assets.clip(a.transition_clip);
    if (!clip.valid()) {
        a.transition_time = -1.0f;
        return false;
    }
    a.transition_time = std::min(a.transition_time + p_dt, a.transition_duration);
    if (_still(r_character)) a.transition_weight = std::clamp(std::min(a.transition_time, a.transition_duration - a.transition_time) / CharacterAnim::TURN_BLEND, 0.0f, 1.0f);
    else a.transition_weight = std::max(a.transition_weight - p_dt / CharacterAnim::TURN_BLEND, 0.0f);
    const float time = a.transition_time;
    if (a.transition_time >= a.transition_duration || (!_still(r_character) && a.transition_weight <= 0.0f)) a.transition_time = -1.0f;
    if (a.transition_weight <= BLEND_MIN_WEIGHT) return a.transition_time >= 0.0f;
    _layer(p_skeleton, clip, time, 0.0f, a.transition_weight, a, r_pose, p_posed);
    return true;
}

static float _stop_settle(const LSkeleton& p_skeleton, const BoundClip& p_clip, const FootIK& p_ik, CharacterAnim& r_anim)
{
    const float duration = p_clip.clip->duration;
    const float fallback = std::max(duration * CharacterAnim::STOP_SETTLE_FALLBACK, CharacterAnim::STOP_SETTLE_MIN);
    if (!p_ik.valid) return fallback;
    const uint32_t samples = CharacterAnim::TURN_CURVE_SAMPLES;
    r_anim._turn_globals.resize(p_skeleton.joint_count());
    float settle = 0.0f;
    for (uint32_t s = 0; s < samples; s++) {
        const float t = duration * (float)s / (float)(samples - 1);
        r_anim._scratch_pose = p_skeleton.rest;
        clip_sample(*p_clip.clip, *p_clip.map, t, r_anim._scratch_pose.data());
        p_skeleton.globals(r_anim._scratch_pose.data(), r_anim._turn_globals.data());
        for (int i = 0; i < 2; i++) {
            if (r_anim._turn_globals[p_ik.foot[i]][3].y - p_ik.rest_height[i] > FootIK::PLANT_HEIGHT) settle = t;
        }
    }
    return settle > 0.0f ? std::clamp(settle, CharacterAnim::STOP_SETTLE_MIN, duration) : fallback;
}

static bool _one_shot(const BoundClip& p_clip, float& r_time, float p_duration, float& r_weight, float p_rate, float p_blend_in, float p_blend_out, bool p_cancel, float p_dt, float& r_sample)
{
    if (r_time < 0.0f) return false;
    if (!p_clip.valid()) {
        r_time = -1.0f;
        return false;
    }
    r_time = std::min(r_time + p_dt * p_rate, p_duration);
    if (p_cancel) r_weight = std::max(r_weight - p_dt / p_blend_out, 0.0f);
    else r_weight = std::clamp(std::min(r_time / p_blend_in, (p_duration - r_time) / (p_blend_out * p_rate)), 0.0f, 1.0f);
    r_sample = r_time;
    if (r_time >= p_duration || (p_cancel && r_weight <= 0.0f)) r_time = -1.0f;
    return r_weight > BLEND_MIN_WEIGHT;
}

static void _stop_and_step(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose, bool& r_posed)
{
    CharacterAnim& a = r_character.anim;
    if (r_character.stop) {
        const FootIK& ik = r_character.ik;
        bool left = a.stand_phase < 0.5f;
        if (ik.planted[0] != ik.planted[1]) left = ik.planted[0];
        const Guid guid = left ? p_asset.stop_left : p_asset.stop_right;
        const BoundClip clip = r_assets.clip(guid);
        if (clip.valid() && clip.clip->duration > 0.0f) {
            a.stop_clip = guid;
            a.stop_time = 0.0f;
            a.stop_duration = clip.clip->duration;
            a.stop_weight = 0.0f;
            a.stop_speed = r_character.speed;
            a.stop_settle = _stop_settle(p_skeleton, clip, r_character.ik, a);
        }
    }
    if (r_character.dynamic_foot >= 0) {
        const Guid guid = r_character.dynamic_foot == 0 ? p_asset.transition_right : p_asset.transition_left;
        const BoundClip clip = r_assets.clip(guid);
        if (clip.valid() && clip.clip->duration > CharacterAnim::DYNAMIC_START) {
            a.dynamic_clip = guid;
            a.dynamic_time = CharacterAnim::DYNAMIC_START;
            a.dynamic_duration = clip.clip->duration;
            a.dynamic_weight = 0.0f;
        }
    }

    float sample = 0.0f;
    const BoundClip stop = r_assets.clip(a.stop_clip);
    const bool stop_cancel = r_character.has_input || r_character.movement == MovementState::IN_AIR;
    if (_one_shot(stop, a.stop_time, a.stop_duration, a.stop_weight, 1.0f, CharacterAnim::STOP_BLEND_IN, CharacterAnim::STOP_BLEND_OUT, stop_cancel, p_dt, sample)) {
        _layer(p_skeleton, stop, sample, 0.0f, a.stop_weight, a, r_pose, r_posed);
        r_posed = true;
    }
    const BoundClip step = r_assets.clip(a.dynamic_clip);
    const bool step_cancel = r_character.has_input || r_character.movement == MovementState::IN_AIR;
    if (_one_shot(step, a.dynamic_time, a.dynamic_duration, a.dynamic_weight, CharacterAnim::DYNAMIC_RATE, CharacterAnim::DYNAMIC_BLEND, CharacterAnim::DYNAMIC_BLEND, step_cancel, p_dt, sample)) {
        _layer(p_skeleton, step, sample, 0.0f, a.dynamic_weight, a, r_pose, r_posed);
        r_posed = true;
    }
}

static void _turn_in_place(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose, bool p_posed, bool p_blocked)
{
    CharacterAnim& a = r_character.anim;
    const bool can_turn = !p_blocked && _still(r_character) && r_character.rotation_mode == RotationMode::LOOKING_DIRECTION;
    const float angle = character_wrap(r_character.aim_yaw - r_character.yaw);
    const CharacterStance& stance = r_character.stance == Stance::CROUCHING ? p_asset.crouching : p_asset.standing;

    if (a.turn_time < 0.0f) {
        if (!can_turn || std::fabs(angle) <= CharacterAnim::TURN_MIN_ANGLE || r_character.aim_yaw_rate >= CharacterAnim::TURN_AIM_RATE_LIMIT) {
            a.turn_delay = 0.0f;
            return;
        }
        a.turn_delay += p_dt;
        const float t = std::clamp((std::fabs(angle) - CharacterAnim::TURN_MIN_ANGLE) / (180.0f - CharacterAnim::TURN_MIN_ANGLE), 0.0f, 1.0f);
        if (a.turn_delay <= t * CharacterAnim::TURN_MAX_DELAY) return;

        const bool big = std::fabs(angle) >= CharacterAnim::TURN_180_THRESHOLD;
        const TurnClip which = angle > 0.0f ? (big ? TurnClip::LEFT_180 : TurnClip::LEFT_90) : (big ? TurnClip::RIGHT_180 : TurnClip::RIGHT_90);
        const Guid guid = stance.turn(which);
        const BoundClip clip = r_assets.clip(guid);
        a.turn_delay = 0.0f;
        if (!clip.valid() || clip.clip->duration <= 0.0f || !_build_yaw_curve(p_skeleton, clip, a, a.turn_curve)) return;
        float total = a.turn_curve.back();
        if (std::fabs(total) < 5.0f) {
            total = std::copysign(big ? 180.0f : 90.0f, angle);
            for (uint32_t s = 0; s < a.turn_curve.size(); s++) a.turn_curve[s] = total * (float)s / (float)(a.turn_curve.size() - 1);
        }
        a.turn_clip = guid;
        a.turn_time = 0.0f;
        a.turn_duration = clip.clip->duration;
        a.turn_scale = angle / total;
        a.turn_applied = 0.0f;
        a.turn_weight = 0.0f;
        a.turn_stopping = false;
    }

    const BoundClip clip = r_assets.clip(a.turn_clip);
    if (!clip.valid()) {
        a.turn_time = -1.0f;
        return;
    }
    if (!can_turn) a.turn_stopping = true;
    a.turn_time = std::min(a.turn_time + p_dt, a.turn_duration);
    const float time = a.turn_time;
    const float yaw = _curve_at(a.turn_curve, a.turn_duration, time);
    if (!a.turn_stopping) {
        r_character.turn_delta += a.turn_scale * yaw - a.turn_applied;
        a.turn_applied = a.turn_scale * yaw;
        a.turn_weight = std::clamp(std::min(a.turn_time, a.turn_duration - a.turn_time) / CharacterAnim::TURN_BLEND, 0.0f, 1.0f);
    } else {
        a.turn_weight = std::max(a.turn_weight - p_dt / CharacterAnim::TURN_BLEND, 0.0f);
    }
    if (a.turn_time >= a.turn_duration || (a.turn_stopping && a.turn_weight <= 0.0f)) a.turn_time = -1.0f;
    if (a.turn_weight <= BLEND_MIN_WEIGHT) return;
    _layer(p_skeleton, clip, time, yaw, a.turn_weight, a, r_pose, p_posed);
}

static void _rotate_in_place(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose, bool p_posed, bool p_blocked)
{
    CharacterAnim& a = r_character.anim;
    const float angle = character_wrap(r_character.aim_yaw - r_character.yaw);
    const bool want = !p_blocked && _still(r_character) && r_character.rotation_mode == RotationMode::AIMING && std::fabs(angle) > CharacterAnim::ROTATE_THRESHOLD;
    const CharacterStance& stance = r_character.stance == Stance::CROUCHING ? p_asset.crouching : p_asset.standing;

    if (want) {
        const Guid guid = angle > 0.0f ? stance.rotate_left : stance.rotate_right;
        if (guid != a.rotate_clip) {
            const BoundClip clip = r_assets.clip(guid);
            if (clip.valid() && clip.clip->duration > 0.0f && _build_yaw_curve(p_skeleton, clip, a, a.rotate_curve)) {
                a.rotate_clip = guid;
                a.rotate_duration = clip.clip->duration;
                a.rotate_time = 0.0f;
                const float total = std::fabs(a.rotate_curve.back());
                a.rotate_speed = total >= 5.0f ? total / a.rotate_duration : CharacterAnim::ROTATE_FALLBACK_SPEED;
                if (total < 5.0f) std::fill(a.rotate_curve.begin(), a.rotate_curve.end(), 0.0f);
            }
        }
        const float t = std::clamp((r_character.aim_yaw_rate - 90.0f) / 180.0f, 0.0f, 1.0f);
        a.rotate_rate = 1.15f + (3.0f - 1.15f) * t;
    }
    const BoundClip clip = r_assets.clip(a.rotate_clip);
    const bool active = want && clip.valid();
    a.rotate_weight = std::clamp(a.rotate_weight + (active ? p_dt : -p_dt) / CharacterAnim::TURN_BLEND, 0.0f, 1.0f);
    if (a.rotate_weight <= BLEND_MIN_WEIGHT || !clip.valid()) {
        if (!want) a.rotate_clip = Guid{};
        return;
    }

    const float step = p_dt * a.rotate_rate;
    a.rotate_time = std::fmod(a.rotate_time + step, a.rotate_duration);
    if (active) {
        const float delta = std::copysign(std::min(a.rotate_speed * step, std::fabs(angle)), angle);
        r_character.turn_delta += delta;
    }
    _layer(p_skeleton, clip, a.rotate_time, _curve_at(a.rotate_curve, a.rotate_duration, a.rotate_time), a.rotate_weight, a, r_pose, p_posed);
}

static quat _between(vec3 p_from, vec3 p_to)
{
    p_from = normalize(p_from);
    p_to = normalize(p_to);
    const float d = dot(p_from, p_to);
    if (d > 0.99999f) return quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.99999f) {
        const vec3 axis = normalize(cross(p_from, std::fabs(p_from.x) < 0.9f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f)));
        return angleAxis(glm::pi<float>(), axis);
    }
    const vec3 c = cross(p_from, p_to);
    return normalize(quat(1.0f + d, c.x, c.y, c.z));
}

static quat _global_rotation(const mat4& p_m)
{
    return normalize(quat_cast(mat3(normalize(vec3(p_m[0])), normalize(vec3(p_m[1])), normalize(vec3(p_m[2])))));
}

static void _resolve_ik(const LLocomotion& p_asset, const LSkeleton& p_skeleton, FootIK& r_ik)
{
    if (r_ik.skeleton == p_skeleton.guid && r_ik.skeleton.valid()) return;
    r_ik.skeleton = p_skeleton.guid;
    r_ik.pelvis = p_skeleton.find(p_asset.pelvis_bone);
    for (int i = 0; i < 3; i++) r_ik.lean[i] = p_skeleton.find(p_asset.lean_bones[i]);
    r_ik.neck = p_skeleton.find(p_asset.neck_bone);
    r_ik.head = p_skeleton.find(p_asset.head_bone);
    for (int i = 0; i < 2; i++) {
        r_ik.thigh[i] = p_skeleton.find(p_asset.thigh_bones[i]);
        r_ik.calf[i] = p_skeleton.find(p_asset.calf_bones[i]);
        r_ik.foot[i] = p_skeleton.find(p_asset.foot_bones[i]);
    }
}

static void _set_global_rotation(const LSkeleton& p_skeleton, const std::vector<mat4>& p_global, int32_t p_joint, const quat& p_rotation, JointTransform* r_local)
{
    const int32_t parent = p_skeleton.parents[p_joint];
    const quat parent_rotation = parent >= 0 ? _global_rotation(p_global[parent]) : quat(1.0f, 0.0f, 0.0f, 0.0f);
    r_local[p_joint].rotation = normalize(inverse(parent_rotation) * p_rotation);
}

void character_foot_ik(const LLocomotion& p_asset, Character& r_character, const LSkeleton& p_skeleton, JointTransform* r_local, std::vector<mat4>& r_global)
{
    FootIK& ik = r_character.ik;
    _resolve_ik(p_asset, p_skeleton, ik);
    bool ok = ik.pelvis >= 0;
    for (int i = 0; i < 2; i++) ok = ok && ik.thigh[i] >= 0 && ik.calf[i] >= 0 && ik.foot[i] >= 0;
    ik.valid = ok;
    if (!ok) return;

    r_global.resize(p_skeleton.joint_count());
    if (!ik.started) {
        p_skeleton.globals(p_skeleton.rest.data(), r_global.data());
        for (int i = 0; i < 2; i++) ik.rest_height[i] = r_global[ik.foot[i]][3].y;
    }
    p_skeleton.globals(r_local, r_global.data());
    for (int i = 0; i < 2; i++) ik.model[i] = vec3(r_global[ik.foot[i]][3]);

    const float air = r_character.movement == MovementState::IN_AIR ? 1.0f : 0.0f;
    const float lean_angle = p_asset.lean_angle + (p_asset.air_lean_angle - p_asset.lean_angle) * air;
    const float roll = glm::radians(r_character.lean_lr * lean_angle);
    const float pitch = glm::radians(r_character.lean_fb * lean_angle);
    uint32_t lean_count = 0;
    for (int i = 0; i < 3; i++) lean_count += ik.lean[i] >= 0 ? 1u : 0u;
    if (lean_count > 0 && (std::fabs(roll) > 1.0e-4f || std::fabs(pitch) > 1.0e-4f)) {
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 forward = facing * vec3(0.0f, 0.0f, 1.0f);
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);
        const quat lean = angleAxis(-pitch, right) * angleAxis(roll, forward);
        const quat part = slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), lean, 1.0f / (float)lean_count);
        for (int i = 0; i < 3; i++) {
            if (ik.lean[i] < 0) continue;
            _set_global_rotation(p_skeleton, r_global, ik.lean[i], part * _global_rotation(r_global[ik.lean[i]]), r_local);
            p_skeleton.globals(r_local, r_global.data());
        }
    }

    if (p_asset.look_at && r_character.look_weight > BLEND_MIN_WEIGHT && (ik.neck >= 0 || ik.head >= 0)) {
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);
        const float yaw = glm::radians(r_character.look_yaw * r_character.look_weight);
        const float up = glm::radians(r_character.look_pitch * r_character.look_weight);
        const bool aiming = r_character.rotation_mode == RotationMode::AIMING;
        const float spine_share = aiming && lean_count > 0 ? 0.6f : 0.0f;
        const float neck_share = ik.head >= 0 ? (aiming ? 0.15f : 0.4f) : (1.0f - spine_share);
        const float head_share = ik.head >= 0 ? 1.0f - spine_share - (ik.neck >= 0 ? neck_share : 0.0f) : 0.0f;
        auto turn = [&](int32_t p_joint, float p_share) {
            if (p_joint < 0 || p_share <= 0.0f) return;
            const quat q = angleAxis(up * p_share, right) * angleAxis(yaw * p_share, vec3(0.0f, 1.0f, 0.0f));
            _set_global_rotation(p_skeleton, r_global, p_joint, q * _global_rotation(r_global[p_joint]), r_local);
            p_skeleton.globals(r_local, r_global.data());
        };
        for (int i = 0; i < 3; i++) turn(ik.lean[i], spine_share / (float)std::max(lean_count, 1u));
        turn(ik.neck, neck_share);
        turn(ik.head, head_share);
    }

    const CharacterAnim& an = r_character.anim;
    const float fb = an.velocity_blend[0] + an.velocity_blend[1];
    const float lr = an.velocity_blend[2] + an.velocity_blend[3];
    const float diagonal = 2.0f * std::min(fb, lr) * CharacterAnim::DIAGONAL_SCALE * (1.0f - an.idle) * (1.0f - an.air_weight);
    const float alpha = p_asset.foot_ik ? ik.alpha : 0.0f;
    if (alpha <= BLEND_MIN_WEIGHT && diagonal <= BLEND_MIN_WEIGHT) return;
    if (ik.pelvis_offset != 0.0f) {
        const int32_t parent = p_skeleton.parents[ik.pelvis];
        const vec3 moved = vec3(r_global[ik.pelvis][3]) + vec3(0.0f, ik.pelvis_offset * alpha, 0.0f);
        r_local[ik.pelvis].translation = parent >= 0 ? vec3(inverse(r_global[parent]) * vec4(moved, 1.0f)) : moved;
        p_skeleton.globals(r_local, r_global.data());
    }

    for (int i = 0; i < 2; i++) {
        const vec3 a = vec3(r_global[ik.thigh[i]][3]);
        const vec3 b = vec3(r_global[ik.calf[i]][3]);
        const vec3 c = vec3(r_global[ik.foot[i]][3]);
        const vec3 pelvis_position = vec3(r_global[ik.pelvis][3]);
        const vec3 inward(pelvis_position.x - c.x, 0.0f, pelvis_position.z - c.z);
        const vec3 target = c + ik.offset[i] * alpha + inward * diagonal;
        const float upper = length(b - a);
        const float lower = length(c - b);
        if (upper < 1.0e-4f || lower < 1.0e-4f) continue;

        const vec3 to_target = target - a;
        const float reach = std::clamp(length(to_target), std::fabs(upper - lower) + 1.0e-3f, upper + lower - 1.0e-3f);
        const vec3 dir = length(to_target) > 1.0e-5f ? normalize(to_target) : normalize(c - a);
        vec3 bend = (b - a) - dir * dot(b - a, dir);
        if (length(bend) < 1.0e-5f) bend = cross(dir, vec3(1.0f, 0.0f, 0.0f));
        bend = normalize(bend);
        const float cos_a = std::clamp((upper * upper + reach * reach - lower * lower) / (2.0f * upper * reach), -1.0f, 1.0f);
        const vec3 knee = a + dir * (upper * cos_a) + bend * (upper * std::sqrt(std::max(1.0f - cos_a * cos_a, 0.0f)));
        const vec3 foot = a + dir * reach;

        const quat thigh_old = _global_rotation(r_global[ik.thigh[i]]);
        const quat calf_old = _global_rotation(r_global[ik.calf[i]]);
        const quat foot_old = _global_rotation(r_global[ik.foot[i]]);
        const quat q1 = _between(b - a, knee - a);
        const quat thigh_new = q1 * thigh_old;
        const quat q2 = _between(q1 * (c - b), foot - knee);
        const quat calf_new = q2 * q1 * calf_old;
        const quat tilt = slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), ik.tilt[i], alpha);
        const quat foot_new = tilt * foot_old;

        _set_global_rotation(p_skeleton, r_global, ik.thigh[i], thigh_new, r_local);
        p_skeleton.globals(r_local, r_global.data());
        _set_global_rotation(p_skeleton, r_global, ik.calf[i], calf_new, r_local);
        p_skeleton.globals(r_local, r_global.data());
        _set_global_rotation(p_skeleton, r_global, ik.foot[i], foot_new, r_local);
        p_skeleton.globals(r_local, r_global.data());
    }
}

static void _air_start(CharacterAnim& r_anim, Guid p_clip, bool p_loop, float p_rate, float p_fade)
{
    if (p_clip == r_anim.air_current.clip && r_anim.air_current.clip.valid()) {
        r_anim.air_current.loop = p_loop;
        return;
    }
    r_anim.air_previous = r_anim.air_current;
    r_anim.air_current = AirClip{ p_clip, 0.0f, p_rate, p_loop };
    r_anim.air_fade = r_anim.air_previous.clip.valid() ? 0.0f : 1.0f;
    r_anim.air_fade_time = p_fade;
}

static void _air_advance(AirClip& r_clip, ClipProvider& r_assets, float p_dt)
{
    const BoundClip clip = r_assets.clip(r_clip.clip);
    if (!clip.valid() || clip.clip->duration <= 0.0f) return;
    r_clip.time += p_dt * r_clip.rate;
    if (r_clip.loop) r_clip.time = std::fmod(r_clip.time, clip.clip->duration);
    else r_clip.time = std::min(r_clip.time, clip.clip->duration);
}

static Guid _fall_clip(const LLocomotion& p_asset, const Character& p_character)
{
    if (p_asset.fall_fast.valid() && -p_character.velocity.y > p_asset.fast_fall_speed) return p_asset.fall_fast;
    return p_asset.fall.valid() ? p_asset.fall : p_asset.fall_fast;
}

static void _update_air(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, float p_dt)
{
    CharacterAnim& a = r_character.anim;
    const bool airborne = r_character.movement == MovementState::IN_AIR;
    const float speed = r_character.speed;

    if (r_character.jumped) {
        const bool left = (a.stand_phase < 0.5f) != p_asset.flip_feet;
        const CharacterStance& stance = p_asset.standing;
        const bool walking = speed <= stance.walk_speed + CharacterAnim::JUMP_WALK_MARGIN;
        const Guid walk = left ? p_asset.jump_walk_left : p_asset.jump_walk_right;
        const Guid walk_other = left ? p_asset.jump_walk_right : p_asset.jump_walk_left;
        const Guid run = left ? p_asset.jump_left : p_asset.jump_right;
        const Guid run_other = left ? p_asset.jump_right : p_asset.jump_left;
        Guid clip;
        if (speed < CharacterAnim::JUMP_MOVING_SPEED && p_asset.jump_standing.valid()) clip = p_asset.jump_standing;
        for (Guid g : { walking ? walk : run, walking ? walk_other : run_other, walking ? run : walk, walking ? run_other : walk_other, p_asset.jump_standing }) {
            if (!clip.valid()) clip = g;
        }
        const float rate = CharacterAnim::JUMP_RATE_MIN + (CharacterAnim::JUMP_RATE_MAX - CharacterAnim::JUMP_RATE_MIN) * std::clamp(speed / CharacterAnim::JUMP_RATE_SPEED, 0.0f, 1.0f);
        a.land_time = -1.0f;
        if (clip.valid()) {
            _air_start(a, clip, false, rate, CharacterAnim::AIR_TAKEOFF_FADE);
            a.air_current.time = 0.0f;
            a.air_state = AirState::TAKEOFF;
        } else {
            a.air_state = AirState::FALL;
        }
    }

    if (airborne) {
        if (a.air_state == AirState::GROUNDED) {
            const bool high = r_character.ground_gap < 0.0f || r_character.ground_gap > CharacterAnim::FALL_GAP;
            if (high || r_character.ungrounded_time >= CharacterAnim::FALL_MIN_TIME) {
                a.air_state = AirState::FALL;
                _air_start(a, _fall_clip(p_asset, r_character), true, 1.0f, CharacterAnim::AIR_FALL_FADE);
            }
        }
        if (a.air_state == AirState::TAKEOFF) {
            const BoundClip takeoff = r_assets.clip(a.air_current.clip);
            const bool done = !takeoff.valid() || a.air_current.time >= takeoff.clip->duration;
            if (done) {
                if (r_character.velocity.y > 0.0f && p_asset.jump_loop.valid()) {
                    a.air_state = AirState::RISE;
                    _air_start(a, p_asset.jump_loop, true, 1.0f, CharacterAnim::AIR_RISE_FADE);
                } else if (_fall_clip(p_asset, r_character).valid()) {
                    a.air_state = AirState::FALL;
                    _air_start(a, _fall_clip(p_asset, r_character), true, 1.0f, CharacterAnim::AIR_FALL_FADE);
                }
            }
        }
        if (a.air_state == AirState::RISE && r_character.velocity.y <= 0.0f) {
            a.air_state = AirState::FALL;
            _air_start(a, _fall_clip(p_asset, r_character), true, 1.0f, CharacterAnim::AIR_FALL_FADE);
        }
        if (a.air_state == AirState::FALL) {
            const Guid fall = _fall_clip(p_asset, r_character);
            if (fall != a.air_current.clip) _air_start(a, fall, true, 1.0f, CharacterAnim::AIR_FAST_FADE);
        }
    }

    if (r_character.landed) {
        const bool was_air = a.air_state != AirState::GROUNDED;
        a.air_state = AirState::GROUNDED;
        a.land_heavy = r_character.land_speed >= p_asset.heavy_land_speed && r_character.has_input && p_asset.land_heavy.valid();
        a.land_clip = a.land_heavy ? p_asset.land_heavy : p_asset.land_light;
        const bool hard_enough = r_character.land_speed >= CharacterAnim::LAND_MIN_SPEED;
        a.land_time = was_air && hard_enough && a.land_clip.valid() ? 0.0f : -1.0f;
        a.land_strength = a.land_heavy ? 1.0f : (r_character.moving ? CharacterAnim::LAND_MOVING_WEIGHT : 1.0f);
    }
    if (!airborne && a.air_state != AirState::GROUNDED) a.air_state = AirState::GROUNDED;

    _air_advance(a.air_current, r_assets, p_dt);
    _air_advance(a.air_previous, r_assets, p_dt);
    a.air_fade = std::min(a.air_fade + p_dt / std::max(a.air_fade_time, 1.0e-3f), 1.0f);

    const bool show = a.air_state != AirState::GROUNDED;
    const float air_speed = show ? CharacterAnim::AIR_JUMP_INTERP : (a.land_time >= 0.0f ? CharacterAnim::AIR_LAND_INTERP : CharacterAnim::AIR_INTERP);
    a.air_weight = a.started ? _interp(a.air_weight, show ? 1.0f : 0.0f, p_dt, air_speed) : (show ? 1.0f : 0.0f);
    if (!show && a.air_weight <= BLEND_MIN_WEIGHT) {
        a.air_current = AirClip{};
        a.air_previous = AirClip{};
    }
}

bool character_pose(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    if (joints == 0) return false;
    CharacterAnim& a = r_character.anim;
    const float speed = r_character.speed;
    const float direction = r_character.direction();

    const float crouch_target = r_character.stance == Stance::CROUCHING ? 1.0f : 0.0f;
    if (!a.started) {
        a.crouch_weight = crouch_target;
    } else {
        a.crouch_weight = _interp(a.crouch_weight, crouch_target, p_dt, CharacterAnim::STANCE_INTERP);
    }
    if (r_character.pivot) a.pivot_time = CharacterAnim::PIVOT_DURATION;
    a.pivot_time = std::max(a.pivot_time - p_dt, 0.0f);
    _update_air(p_asset, r_character, r_assets, p_dt);

    bool crossing = true;
    if (r_character.ik.valid) {
        const vec3 right = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(-1.0f, 0.0f, 0.0f);
        crossing = std::fabs(dot(r_character.ik.model[0] - r_character.ik.model[1], right)) < CharacterAnim::HIPS_CROSS_DISTANCE;
    }
    _update_directions(a, direction, r_character.moving, speed, p_dt, !a.started || p_dt <= 0.0f, crossing);
    a.idle_time += p_dt;
    a._stand_pose = p_skeleton.rest;
    a._crouch_pose = p_skeleton.rest;
    const bool stand = a.crouch_weight < 1.0f - BLEND_MIN_WEIGHT && _stance_pose(p_asset.standing, a, a.stand_phase, CharacterAnim::STANDING_MAX_RATE, r_assets, r_evaluator, p_skeleton, speed, p_dt, a._stand_pose.data());
    const bool crouch = a.crouch_weight > BLEND_MIN_WEIGHT && _stance_pose(p_asset.crouching, a, a.crouch_phase, CharacterAnim::CROUCHING_MAX_RATE, r_assets, r_evaluator, p_skeleton, speed, p_dt, a._crouch_pose.data());
    a.started = true;
    bool posed = stand || crouch;
    if (stand && crouch) {
        const PoseInput inputs[2] = { { a._stand_pose.data(), 1.0f - a.crouch_weight }, { a._crouch_pose.data(), a.crouch_weight } };
        pose_blend(inputs, 2, joints, r_pose);
    } else if (stand) {
        std::copy(a._stand_pose.begin(), a._stand_pose.end(), r_pose);
    } else if (crouch) {
        std::copy(a._crouch_pose.begin(), a._crouch_pose.end(), r_pose);
    }

    const bool transitioning = _stance_transition(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed);
    posed = posed || a.transition_weight > BLEND_MIN_WEIGHT;
    _turn_in_place(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed, transitioning);
    posed = posed || a.turn_weight > BLEND_MIN_WEIGHT;
    _rotate_in_place(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed, transitioning);
    posed = posed || a.rotate_weight > BLEND_MIN_WEIGHT;
    _stop_and_step(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed);

    if (a.air_weight > BLEND_MIN_WEIGHT) {
        const BoundClip current = r_assets.clip(a.air_current.clip);
        if (current.valid() && current.clip->duration > 0.0f) {
            a._air_pose = p_skeleton.rest;
            clip_sample(*current.clip, *current.map, a.air_current.time, a._air_pose.data());
            const BoundClip previous = r_assets.clip(a.air_previous.clip);
            if (a.air_fade < 1.0f && previous.valid() && previous.clip->duration > 0.0f) {
                a._air_previous_pose = p_skeleton.rest;
                clip_sample(*previous.clip, *previous.map, a.air_previous.time, a._air_previous_pose.data());
                const PoseInput inputs[2] = { { a._air_previous_pose.data(), 1.0f - a.air_fade }, { a._air_pose.data(), a.air_fade } };
                pose_blend(inputs, 2, joints, a._air_pose.data());
            }
            if (posed) _mix(r_pose, a._air_pose.data(), a.air_weight, joints);
            else std::copy(a._air_pose.begin(), a._air_pose.end(), r_pose);
            posed = true;
        }
    }

    if (a.land_time >= 0.0f) {
        const BoundClip land = r_assets.clip(a.land_clip);
        if (!land.valid() || land.clip->duration <= 0.0f || a.land_time >= land.clip->duration || r_character.movement == MovementState::IN_AIR) {
            a.land_time = -1.0f;
        } else {
            const float duration = land.clip->duration;
            if (r_character.has_input && r_character.speed > CharacterAnim::LAND_RUN_SPEED) a.land_strength = std::max(a.land_strength - p_dt / (a.land_heavy ? CharacterAnim::LAND_HEAVY_RUN_FADE : CharacterAnim::LAND_RUN_FADE), 0.0f);
            const float blend_in = std::clamp(a.land_time / CharacterAnim::LAND_BLEND_IN, 0.0f, 1.0f);
            const float blend_out = std::clamp((duration - a.land_time) / (duration * CharacterAnim::LAND_BLEND_OUT), 0.0f, 1.0f);
            const float weight = std::min(blend_in, blend_out) * a.land_strength;
            a._land_pose = p_skeleton.rest;
            clip_sample(*land.clip, *land.map, a.land_time, a._land_pose.data());
            if (posed) _mix(r_pose, a._land_pose.data(), weight, joints);
            else std::copy(a._land_pose.begin(), a._land_pose.end(), r_pose);
            posed = true;
            a.land_time += p_dt;
        }
    }
    return posed;
}

}
