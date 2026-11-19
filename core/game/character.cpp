#include <core/game/character.h>
#include <core/animation/clip_player.h>
#include <core/animation/pose_blend.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

vec3 CharacterAction::root_at(float p_time) const
{
    if (root.empty()) return vec3(0.0f);
    const float f = std::clamp(p_time / CURVE_STEP, 0.0f, (float)(root.size() - 1));
    const uint32_t i = std::min((uint32_t)f, (uint32_t)root.size() - 1);
    const uint32_t j = std::min(i + 1, (uint32_t)root.size() - 1);
    return mix(root[i], root[j], f - (float)i);
}

void CharacterAction::start(Guid p_clip, float p_yaw, const vec3& p_velocity)
{
    if (p_clip != clip) {
        root.clear();
        duration = 0.0f;
    }
    clip = p_clip;
    time = 0.0f;
    weight = 0.0f;
    fade = -1.0f;
    yaw = p_yaw;
    start_velocity = vec3(p_velocity.x, 0.0f, p_velocity.z);
}

float CharacterAction::momentum() const
{
    if (average_speed <= 0.1f) return 1.0f;
    return std::clamp(length(start_velocity) / average_speed, 1.0f, MAX_MOMENTUM);
}

void CharacterAction::exit()
{
    if (fade < 0.0f) fade = 0.0f;
}

void CharacterAction::stop()
{
    time = -1.0f;
    weight = 0.0f;
    fade = -1.0f;
}

void CharacterAction::bind(ClipProvider& r_assets, const LSkeleton& p_skeleton)
{
    if (!active() || duration > 0.0f) return;
    const BoundClip bound = r_assets.clip(clip);
    if (!bound.valid() || bound.clip->duration <= 0.0f) {
        stop();
        return;
    }
    int32_t track = -1;
    for (uint32_t t = 0; t < (uint32_t)bound.map->size() && track < 0; t++) {
        const int32_t joint = (*bound.map)[t];
        if (joint >= 0 && p_skeleton.parents[joint] < 0) track = (int32_t)t;
    }
    root.clear();
    for (float t = 0.0f; t < bound.clip->duration + CURVE_STEP; t += CURVE_STEP) root.push_back(track >= 0 ? bound.clip->sample((uint32_t)track, std::min(t, bound.clip->duration)).translation : vec3(0.0f));
    duration = bound.clip->duration;
    const vec3 total = root.back() - root.front();
    const float distance = length(vec2(total.x, total.z));
    exit_time = duration * EXIT_FALLBACK;
    if (distance > 0.1f) {
        for (size_t i = 0; i < root.size(); i++) {
            const vec3 moved = root[i] - root.front();
            if (length(vec2(moved.x, moved.z)) < distance * EXIT_SHARE) continue;
            exit_time = (float)i * CURVE_STEP;
            break;
        }
    }
    exit_time = std::min(exit_time, std::max(duration - BLEND_OUT, 0.0f));
    average_speed = exit_time > 0.0f ? distance * EXIT_SHARE / exit_time : 0.0f;
}

bool CharacterAction::layer(ClipProvider& r_assets, const LSkeleton& p_skeleton, std::vector<JointTransform>& r_scratch, JointTransform* r_pose, bool p_posed)
{
    if (!active() || duration <= 0.0f) return false;
    const BoundClip bound = r_assets.clip(clip);
    if (!bound.valid()) return false;
    weight = std::clamp(std::min(time / BLEND_IN, (duration - time) / BLEND_OUT), 0.0f, 1.0f);
    if (fade >= 0.0f) weight *= std::clamp(1.0f - fade / BLEND_OUT, 0.0f, 1.0f);
    if (weight <= BLEND_MIN_WEIGHT) return false;
    r_scratch = p_skeleton.rest;
    clip_sample(bound, time, r_scratch.data());
    const vec3 hold = root.empty() ? vec3(0.0f) : root.front();
    const uint32_t joints = p_skeleton.joint_count();
    for (uint32_t j = 0; j < joints; j++) {
        if (p_skeleton.parents[j] < 0) r_scratch[j].translation = hold;
    }
    if (!p_posed) {
        std::copy(r_scratch.begin(), r_scratch.end(), r_pose);
        return true;
    }
    const PoseInput inputs[2] = { { r_pose, 1.0f - weight }, { r_scratch.data(), weight } };
    _blend.resize(joints);
    pose_blend(inputs, 2, joints, _blend.data());
    std::copy(_blend.begin(), _blend.end(), r_pose);
    return true;
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

static void _sample(const BoundClip& p_clip, float p_time, bool p_mirror, CharacterAnim& r_anim, const LSkeleton& p_skeleton, JointTransform* r_pose)
{
    clip_sample(p_clip, p_time, r_pose);
    if (p_mirror) r_anim.mirror.apply(p_skeleton, r_pose);
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

static bool _stance_pose(const CharacterStance& p_stance, CharacterAnim& r_anim, float& r_phase, float& r_phase_speed, float& r_stride, float p_rush, float p_max_rate, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float p_speed, float p_dt, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    const float* vb = r_anim.velocity_blend;
    const float h = r_anim.hips_forward;
    float weights[CYCLE_DIRECTION_COUNT] = { vb[0], vb[1], vb[2] * h, vb[2] * (1.0f - h), vb[3] * h, vb[3] * (1.0f - h) };
    const LBlendSpace* spaces[CYCLE_DIRECTION_COUNT];
    bool mirrored[CYCLE_DIRECTION_COUNT];
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) {
        const uint32_t opposite = (uint32_t)cycle_direction_opposite((CycleDirection)d);
        spaces[d] = r_assets.blend_space(p_stance.cycles[d]);
        mirrored[d] = false;
        if (!spaces[d] && opposite != d) {
            spaces[d] = r_assets.blend_space(p_stance.cycles[opposite]);
            mirrored[d] = spaces[d] != nullptr;
        }
    }
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
        const float ratio = speed > 0.0f ? p_speed / speed : 1.0f;
        r_stride = std::clamp(ratio, CharacterAnim::STRIDE_MIN, CharacterAnim::STRIDE_MAX);
        r_anim.play_rate = std::clamp(ratio / r_stride, 0.0f, p_max_rate);
        r_phase_speed = r_anim.play_rate / (duration / total);
        r_phase += p_dt * r_phase_speed * p_rush;
        r_phase -= std::floor(r_phase);
    }

    std::vector<PoseInput> inputs;
    const BoundClip idle = r_assets.clip(p_stance.idle);
    const bool has_idle = idle.valid() && idle.clip->duration > 0.0f;
    const float moving = total > 0.0f ? (has_idle ? 1.0f - r_anim.idle : 1.0f) : 0.0f;
    if (has_idle && (r_anim.idle > BLEND_MIN_WEIGHT || total <= 0.0f)) {
        std::vector<JointTransform>& pose = r_anim._cycle_poses[CYCLE_DIRECTION_COUNT];
        pose = p_skeleton.rest;
        clip_sample(idle, std::fmod(r_anim.idle_time, idle.clip->duration), pose.data());
        inputs.push_back({ pose.data(), total > 0.0f ? r_anim.idle : 1.0f });
    }
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT && total > 0.0f; d++) {
        const float w = moving * weights[d] / total;
        if (w <= BLEND_MIN_WEIGHT) continue;
        std::vector<JointTransform>& pose = r_anim._cycle_poses[d];
        pose = p_skeleton.rest;
        const float phase = mirrored[d] ? r_phase + 0.5f - std::floor(r_phase + 0.5f) : r_phase;
        if (!r_evaluator.blend_sample(r_assets, p_skeleton, *spaces[d], value, phase, pose.data())) continue;
        if (mirrored[d]) r_anim.mirror.apply(p_skeleton, pose.data());
        inputs.push_back({ pose.data(), w });
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

static bool _build_yaw_curve(const LSkeleton& p_skeleton, const BoundClip& p_clip, bool p_mirror, CharacterAnim& r_anim, std::vector<float>& r_curve, float& r_offset)
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
    std::vector<mat4> rest(joints);
    p_skeleton.globals(p_skeleton.rest.data(), rest.data());
    for (uint32_t s = 0; s < samples; s++) {
        r_anim._scratch_pose = p_skeleton.rest;
        _sample(p_clip, p_clip.clip->duration * (float)s / (float)(samples - 1), p_mirror, r_anim, p_skeleton, r_anim._scratch_pose.data());
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
    uint32_t best_depth = UINT32_MAX;
    for (size_t c = 0; c < candidates.size(); c++) {
        const float total = yaws[c * samples + samples - 1] - yaws[c * samples];
        const uint32_t d = depth[candidates[c]];
        const bool turns = std::fabs(total) >= CharacterAnim::TURN_CURVE_MIN;
        const bool best_turns = std::fabs(best_total) >= CharacterAnim::TURN_CURVE_MIN;
        const bool better = turns ? (!best_turns || d < best_depth || (d == best_depth && std::fabs(total) > std::fabs(best_total))) : (!best_turns && std::fabs(total) > std::fabs(best_total));
        if (better) {
            best_total = total;
            best = c;
            best_depth = d;
        }
    }
    r_curve.resize(samples);
    for (uint32_t s = 0; s < samples; s++) r_curve[s] = yaws[best * samples + s] - yaws[best * samples];
    const mat4& reference = rest[candidates[best]];
    const float rest_yaw = _flat_yaw(normalize(vec3(use_z[best] ? reference[2] : reference[0])));
    r_offset = character_wrap(yaws[best * samples] - rest_yaw);
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

static void _layer(const LSkeleton& p_skeleton, const BoundClip& p_clip, float p_time, bool p_mirror, float p_counter_yaw, float p_weight, CharacterAnim& r_anim, JointTransform* r_pose, bool p_posed)
{
    r_anim._scratch_pose = p_skeleton.rest;
    _sample(p_clip, p_time, p_mirror, r_anim, p_skeleton, r_anim._scratch_pose.data());
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
            float start = 0.0f;
            if (a.transition_time >= 0.0f && a.transition_weight > BLEND_MIN_WEIGHT) {
                CharacterAnim::FadingClip* slot = &a.transition_fading[0];
                for (CharacterAnim::FadingClip& f : a.transition_fading) {
                    if (f.weight < slot->weight) slot = &f;
                }
                *slot = { a.transition_clip, a.transition_time, a.transition_duration, a.transition_weight };
                if (a.transition_duration > 0.0f) start = clip.clip->duration * std::clamp(1.0f - a.transition_time / a.transition_duration, 0.0f, 1.0f);
            }
            a.transition_clip = guid;
            a.transition_time = start;
            a.transition_elapsed = 0.0f;
            a.transition_duration = clip.clip->duration;
            a.transition_weight = 1.0f;
            a.crouch_weight = r_character.stance == Stance::CROUCHING ? 1.0f : 0.0f;
            a.inert_request = true;
            a.turn_stopping = true;
        }
    }

    bool fading = false;
    for (CharacterAnim::FadingClip& f : a.transition_fading) {
        if (f.weight <= 0.0f) continue;
        f.weight = std::max(f.weight - p_dt / CharacterAnim::TURN_BLEND, 0.0f);
        f.time = std::min(f.time + p_dt, f.duration);
        const BoundClip faded = r_assets.clip(f.clip);
        if (!faded.valid() || f.weight <= BLEND_MIN_WEIGHT) {
            f.weight = 0.0f;
            continue;
        }
        _layer(p_skeleton, faded, f.time, false, 0.0f, f.weight, a, r_pose, p_posed);
        p_posed = true;
        fading = true;
    }

    if (a.transition_time < 0.0f) return fading;
    const BoundClip clip = r_assets.clip(a.transition_clip);
    if (!clip.valid()) {
        a.transition_time = -1.0f;
        return fading;
    }
    a.transition_time = std::min(a.transition_time + p_dt, a.transition_duration);
    a.transition_elapsed += p_dt;
    if (_still(r_character)) a.transition_weight = std::clamp((a.transition_duration - a.transition_time) / CharacterAnim::TURN_BLEND, 0.0f, 1.0f);
    else a.transition_weight = std::max(a.transition_weight - p_dt / CharacterAnim::TURN_BLEND, 0.0f);
    const float time = a.transition_time;
    if (a.transition_time >= a.transition_duration || (!_still(r_character) && a.transition_weight <= 0.0f)) a.transition_time = -1.0f;
    if (a.transition_weight <= BLEND_MIN_WEIGHT) return fading || a.transition_time >= 0.0f;
    _layer(p_skeleton, clip, time, false, 0.0f, a.transition_weight, a, r_pose, p_posed);
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
        clip_sample(p_clip, t, r_anim._scratch_pose.data());
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
        const SideClip side = left ? side_clip(p_asset.stop_left, p_asset.stop_right) : side_clip(p_asset.stop_right, p_asset.stop_left);
        const BoundClip clip = r_assets.clip(side.clip);
        if (clip.valid() && clip.clip->duration > 0.0f) {
            a.stop_clip = side.clip;
            a.stop_mirror = side.mirror;
            a.stop_time = 0.0f;
            a.stop_duration = clip.clip->duration;
            a.stop_weight = 0.0f;
            a.inert_request = true;
            a.stop_speed = r_character.speed;
            a.stop_settle = _stop_settle(p_skeleton, clip, r_character.ik, a);
        }
    }
    if (r_character.dynamic_foot >= 0) {
        const SideClip side = r_character.dynamic_foot == 0 ? side_clip(p_asset.transition_right, p_asset.transition_left) : side_clip(p_asset.transition_left, p_asset.transition_right);
        const BoundClip clip = r_assets.clip(side.clip);
        if (clip.valid() && clip.clip->duration > CharacterAnim::DYNAMIC_START) {
            a.dynamic_clip = side.clip;
            a.dynamic_mirror = side.mirror;
            a.dynamic_time = CharacterAnim::DYNAMIC_START;
            a.dynamic_duration = clip.clip->duration;
            a.dynamic_weight = 0.0f;
            a.inert_request = true;
        }
    }

    float sample = 0.0f;
    const BoundClip stop = r_assets.clip(a.stop_clip);
    const bool stop_cancel = r_character.has_input || r_character.movement == MovementState::IN_AIR;
    if (_one_shot(stop, a.stop_time, a.stop_duration, a.stop_weight, 1.0f, CharacterAnim::STOP_BLEND_IN, CharacterAnim::STOP_BLEND_OUT, stop_cancel, p_dt, sample)) {
        _layer(p_skeleton, stop, sample, a.stop_mirror, 0.0f, a.stop_weight, a, r_pose, r_posed);
        r_posed = true;
    }
    const BoundClip step = r_assets.clip(a.dynamic_clip);
    const bool step_cancel = r_character.has_input || r_character.movement == MovementState::IN_AIR;
    if (_one_shot(step, a.dynamic_time, a.dynamic_duration, a.dynamic_weight, CharacterAnim::DYNAMIC_RATE, CharacterAnim::DYNAMIC_BLEND_IN, CharacterAnim::DYNAMIC_BLEND, step_cancel, p_dt, sample)) {
        _layer(p_skeleton, step, sample, a.dynamic_mirror, 0.0f, a.dynamic_weight, a, r_pose, r_posed);
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
        if (a.turn_delay <= (1.0f - t) * CharacterAnim::TURN_MAX_DELAY) return;

        const bool big = std::fabs(angle) >= CharacterAnim::TURN_180_THRESHOLD;
        const TurnClip which = angle > 0.0f ? (big ? TurnClip::LEFT_180 : TurnClip::LEFT_90) : (big ? TurnClip::RIGHT_180 : TurnClip::RIGHT_90);
        const SideClip side = side_clip(stance.turn(which), stance.turn(turn_clip_opposite(which)));
        const BoundClip clip = r_assets.clip(side.clip);
        a.turn_delay = 0.0f;
        if (!clip.valid() || clip.clip->duration <= 0.0f || !_build_yaw_curve(p_skeleton, clip, side.mirror, a, a.turn_curve, a.turn_offset)) return;
        float total = a.turn_curve.back();
        a.turn_measured = std::fabs(total) >= 5.0f;
        if (!a.turn_measured) {
            total = std::copysign(big ? 180.0f : 90.0f, angle);
            for (uint32_t s = 0; s < a.turn_curve.size(); s++) a.turn_curve[s] = total * (float)s / (float)(a.turn_curve.size() - 1);
        }
        a.turn_clip = side.clip;
        a.turn_mirror = side.mirror;
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
    _layer(p_skeleton, clip, time, a.turn_mirror, (a.turn_measured ? yaw : 0.0f) + a.turn_offset, a.turn_weight, a, r_pose, p_posed);
}

static void _rotate_in_place(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose, bool p_posed, bool p_blocked)
{
    CharacterAnim& a = r_character.anim;
    const float angle = character_wrap(r_character.aim_yaw - r_character.yaw);
    const bool want = !p_blocked && _still(r_character) && r_character.rotation_mode == RotationMode::AIMING && std::fabs(angle) > CharacterAnim::ROTATE_THRESHOLD;
    const CharacterStance& stance = r_character.stance == Stance::CROUCHING ? p_asset.crouching : p_asset.standing;

    if (want) {
        const SideClip side = angle > 0.0f ? side_clip(stance.rotate_left, stance.rotate_right) : side_clip(stance.rotate_right, stance.rotate_left);
        if (side.clip != a.rotate_clip || side.mirror != a.rotate_mirror) {
            const BoundClip clip = r_assets.clip(side.clip);
            if (clip.valid() && clip.clip->duration > 0.0f && _build_yaw_curve(p_skeleton, clip, side.mirror, a, a.rotate_curve, a.rotate_offset)) {
                a.rotate_clip = side.clip;
                a.rotate_mirror = side.mirror;
                a.rotate_duration = clip.clip->duration;
                a.rotate_time = 0.0f;
                const float total = a.rotate_curve.back();
                a.rotate_speed = std::fabs(total) >= 5.0f ? std::fabs(total) / a.rotate_duration : CharacterAnim::ROTATE_FALLBACK_SPEED;
                a.rotate_sign = (total >= 0.0f) == (angle >= 0.0f) ? 1.0f : -1.0f;
                if (std::fabs(total) < 5.0f) std::fill(a.rotate_curve.begin(), a.rotate_curve.end(), 0.0f);
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
    const float before = _curve_at(a.rotate_curve, a.rotate_duration, a.rotate_time);
    const float next = a.rotate_time + step;
    const bool wrapped = next >= a.rotate_duration;
    a.rotate_time = std::fmod(next, a.rotate_duration);
    const float after = _curve_at(a.rotate_curve, a.rotate_duration, a.rotate_time);
    const bool curved = a.rotate_curve.size() >= 2 && std::fabs(a.rotate_curve.back()) >= 5.0f;
    float delta = curved ? (after - before + (wrapped ? a.rotate_curve.back() : 0.0f)) * a.rotate_sign : std::copysign(a.rotate_speed * step, angle);
    if (active && std::fabs(delta) > std::fabs(angle)) delta = std::copysign(std::fabs(angle), delta);
    r_character.turn_delta += delta * a.rotate_weight;
    _layer(p_skeleton, clip, a.rotate_time, a.rotate_mirror, after + a.rotate_offset, a.rotate_weight, a, r_pose, p_posed);
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
    for (int i = 0; i < 2; i++) r_ik.upperarm[i] = p_skeleton.find(p_asset.upperarm_bones[i]);
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

static void _refresh(const LSkeleton& p_skeleton, const JointTransform* p_local, std::vector<mat4>& r_global, int32_t p_joint)
{
    thread_local std::vector<uint8_t> dirty;
    const uint32_t n = p_skeleton.joint_count();
    dirty.assign(n, 0);
    const int32_t parent = p_skeleton.parents[p_joint];
    r_global[p_joint] = parent >= 0 ? r_global[parent] * p_local[p_joint].matrix() : p_local[p_joint].matrix();
    dirty[p_joint] = 1;
    for (uint32_t j = (uint32_t)p_joint + 1; j < n; j++) {
        const int32_t up = p_skeleton.parents[j];
        if (up < 0 || !dirty[up]) continue;
        r_global[j] = r_global[up] * p_local[j].matrix();
        dirty[j] = 1;
    }
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
        for (int i = 0; i < 2; i++) {
            ik.rest_height[i] = r_global[ik.foot[i]][3].y;
            const vec3 ahead = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
            ik.toe_local[i] = normalize(inverse(_global_rotation(r_global[ik.foot[i]])) * ahead);
        }
    }
    p_skeleton.globals(r_local, r_global.data());
    vec3 stride_warp[2] = { vec3(0.0f), vec3(0.0f) };
    {
        const CharacterAnim& sa = r_character.anim;
        const float stride = glm::mix(sa.stand_stride, sa.crouch_stride, sa.crouch_weight);
        const float moving = (1.0f - sa.idle) * (1.0f - sa.air_proc);
        const vec3 flat_velocity(r_character.velocity.x, 0.0f, r_character.velocity.z);
        if (moving > BLEND_MIN_WEIGHT && std::fabs(stride - 1.0f) > 1.0e-3f && length(flat_velocity) > 0.1f) {
            const vec3 travel = normalize(inverse(angleAxis(glm::radians(r_character.yaw + r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f))) * flat_velocity);
            const vec3 hips(r_global[ik.pelvis][3].x, 0.0f, r_global[ik.pelvis][3].z);
            for (int i = 0; i < 2; i++) {
                const vec3 foot(r_global[ik.foot[i]][3].x, 0.0f, r_global[ik.foot[i]][3].z);
                stride_warp[i] = travel * (dot(foot - hips, travel) * (stride - 1.0f) * moving);
            }
        }
    }
    for (int i = 0; i < 2; i++) ik.model[i] = vec3(r_global[ik.foot[i]][3]) + stride_warp[i];

    vec3 sway_shift[2] = { vec3(0.0f), vec3(0.0f) };
    const float sway_angle = length(r_character.sway);
    const bool swaying = sway_angle > 1.0e-3f;
    if (swaying) {
        const quat entity = angleAxis(glm::radians(r_character.yaw + r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 axis = inverse(entity) * (r_character.sway / sway_angle);
        const quat tip = angleAxis(sway_angle, axis);
        const vec3 pivot = (ik.model[0] + ik.model[1]) * 0.5f;
        const int32_t parent = p_skeleton.parents[ik.pelvis];
        const vec3 pelvis_position = pivot + tip * (vec3(r_global[ik.pelvis][3]) - pivot);
        const quat pelvis_rotation = tip * _global_rotation(r_global[ik.pelvis]);
        r_local[ik.pelvis].translation = parent >= 0 ? vec3(inverse(r_global[parent]) * vec4(pelvis_position, 1.0f)) : pelvis_position;
        _set_global_rotation(p_skeleton, r_global, ik.pelvis, pelvis_rotation, r_local);
        _refresh(p_skeleton, r_local, r_global, ik.pelvis);
        uint32_t spine = 0;
        for (int i = 0; i < 3; i++) spine += ik.lean[i] >= 0 ? 1u : 0u;
        if (spine > 0) {
            const quat bend = angleAxis(sway_angle * CharacterRagdoll::SWAY_TORSO_BEND / (float)spine, axis);
            for (int i = 0; i < 3; i++) {
                if (ik.lean[i] < 0) continue;
                _set_global_rotation(p_skeleton, r_global, ik.lean[i], bend * _global_rotation(r_global[ik.lean[i]]), r_local);
                _refresh(p_skeleton, r_local, r_global, ik.lean[i]);
            }
        }
        for (int i = 0; i < 2; i++) sway_shift[i] = vec3(r_global[ik.foot[i]][3]) - ik.model[i];
    }

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
            _refresh(p_skeleton, r_local, r_global, ik.lean[i]);
        }
    }

    if (p_asset.look_at && r_character.look_weight > BLEND_MIN_WEIGHT && (ik.neck >= 0 || ik.head >= 0)) {
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);
        const float yaw = glm::radians(r_character.look_yaw * r_character.look_weight);
        const float up = glm::radians(r_character.look_pitch * r_character.look_weight);
        const float aim = r_character.aim_weight;
        const float spine_share = lean_count > 0 ? 0.6f * aim : 0.0f;
        const float neck_share = ik.head >= 0 ? 0.4f + (0.15f - 0.4f) * aim : (1.0f - spine_share);
        const float head_share = ik.head >= 0 ? 1.0f - spine_share - (ik.neck >= 0 ? neck_share : 0.0f) : 0.0f;
        auto turn = [&](int32_t p_joint, float p_share) {
            if (p_joint < 0 || p_share <= 0.0f) return;
            const quat q = angleAxis(up * p_share, right) * angleAxis(yaw * p_share, vec3(0.0f, 1.0f, 0.0f));
            _set_global_rotation(p_skeleton, r_global, p_joint, q * _global_rotation(r_global[p_joint]), r_local);
            _refresh(p_skeleton, r_local, r_global, p_joint);
        };
        for (int i = 0; i < 3; i++) turn(ik.lean[i], spine_share / (float)std::max(lean_count, 1u));
        turn(ik.neck, neck_share);
        turn(ik.head, head_share);
    }

    const CharacterAnim& an = r_character.anim;
    const float air_blend = an.air_proc;
    const float flail_phase = an.air_time * CharacterAnim::FLAIL_FREQUENCY * glm::two_pi<float>();
    if (air_blend > BLEND_MIN_WEIGHT && (an.air_arms > 0.01f || an.air_flail > 0.001f)) {
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);
        const vec3 hips = vec3(r_global[ik.pelvis][3]);
        for (int i = 0; i < 2; i++) {
            if (ik.upperarm[i] < 0) continue;
            const vec3 shoulder = vec3(r_global[ik.upperarm[i]][3]);
            const vec3 outward = right * (dot(shoulder - hips, right) >= 0.0f ? 1.0f : -1.0f);
            const vec3 raise_axis = normalize(cross(vec3(0.0f, -1.0f, 0.0f), outward));
            const float wave = std::sin(flail_phase + (float)i * 1.9f);
            const float swing = std::cos(flail_phase * 0.8f + (float)i * glm::pi<float>());
            const float raise = glm::radians(an.air_arms + an.air_flail * CharacterAnim::FLAIL_ARM_ANGLE * wave) * air_blend;
            const float swing_angle = glm::radians(an.air_flail * CharacterAnim::FLAIL_SWING_ANGLE * swing) * air_blend;
            const quat q = angleAxis(swing_angle, right) * angleAxis(raise, raise_axis);
            _set_global_rotation(p_skeleton, r_global, ik.upperarm[i], q * _global_rotation(r_global[ik.upperarm[i]]), r_local);
            _refresh(p_skeleton, r_local, r_global, ik.upperarm[i]);
        }
    }
    vec3 air_shift[2] = { vec3(0.0f), vec3(0.0f) };
    const float leg_motion = an.air_flail;
    const bool air_legs = air_blend > BLEND_MIN_WEIGHT && leg_motion > 0.001f;
    if (air_legs) {
        const vec3 forward = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 2; i++) {
            const float cycle = flail_phase + (float)i * glm::pi<float>();
            const float lift = leg_motion * CharacterAnim::FLAIL_LEG_LIFT * std::max(std::cos(cycle), 0.0f);
            air_shift[i] = (vec3(0.0f, lift, 0.0f) + forward * (leg_motion * CharacterAnim::FLAIL_LEG_STRIDE * std::sin(cycle))) * air_blend;
        }
    }
    int reach_foot = -1;
    float reach_weight = 0.0f;
    vec3 reach_forward(0.0f, 0.0f, 1.0f);
    if (r_character.land_known && air_blend > BLEND_MIN_WEIGHT && r_character.land_eta < CharacterAnim::LAND_REACH_TIME && r_character.ungrounded_time + r_character.land_eta >= CharacterAnim::LAND_REACH_MIN_AIR) {
        const float k = 1.0f - r_character.land_eta / CharacterAnim::LAND_REACH_TIME;
        reach_weight = k * k * (3.0f - 2.0f * k) * air_blend;
        const quat to_model = inverse(angleAxis(glm::radians(r_character.yaw + r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)));
        const vec3 travel = to_model * vec3(r_character.velocity.x, 0.0f, r_character.velocity.z);
        reach_forward = length(travel) > 0.5f ? normalize(travel) : angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
        if (an.jump_foot >= 0) reach_foot = 1 - an.jump_foot;
        else reach_foot = dot(vec3(r_global[ik.foot[0]][3]), reach_forward) >= dot(vec3(r_global[ik.foot[1]][3]), reach_forward) ? 0 : 1;
        r_character.anim.roll_foot = (int8_t)reach_foot;
    }
    int roll_foot = -1;
    float roll_weight = 0.0f;
    if (reach_foot < 0 && an.roll_foot >= 0 && an.land_roll > 0.0f) {
        roll_foot = an.roll_foot;
        roll_weight = an.land_roll * an.land_roll * (3.0f - 2.0f * an.land_roll);
    }
    if (reach_foot >= 0) {
        const vec3 brace_axis = normalize(cross(vec3(0.0f, -1.0f, 0.0f), reach_forward));
        const quat brace = angleAxis(glm::radians(CharacterAnim::BRACE_ARM_ANGLE) * reach_weight, brace_axis);
        for (int i = 0; i < 2; i++) {
            if (ik.upperarm[i] < 0) continue;
            _set_global_rotation(p_skeleton, r_global, ik.upperarm[i], brace * _global_rotation(r_global[ik.upperarm[i]]), r_local);
            _refresh(p_skeleton, r_local, r_global, ik.upperarm[i]);
        }
    }
    const float fb = an.velocity_blend[0] + an.velocity_blend[1];
    const float lr = an.velocity_blend[2] + an.velocity_blend[3];
    const float diagonal = 2.0f * std::min(fb, lr) * CharacterAnim::DIAGONAL_SCALE * (1.0f - an.idle) * (1.0f - an.air_weight);
    const float alpha = p_asset.foot_ik ? ik.alpha : 0.0f;
    const bool sinking = std::fabs(an.sink) > 1.0e-4f;
    const bool warping = length(stride_warp[0]) + length(stride_warp[1]) > 1.0e-4f;
    {
        const quat body_yaw = angleAxis(glm::radians(r_character.yaw + r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const quat to_model = inverse(body_yaw);
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);

        const vec3 flinch = to_model * r_character.flinch;
        if (length(flinch) > 1.0e-4f) {
            uint32_t spine = 0;
            for (int i = 0; i < 3; i++) spine += ik.lean[i] >= 0 ? 1u : 0u;
            const float angle = length(flinch);
            const vec3 axis = flinch / angle;
            for (int i = 0; i < 3 && spine > 0; i++) {
                if (ik.lean[i] < 0) continue;
                _set_global_rotation(p_skeleton, r_global, ik.lean[i], angleAxis(angle / (float)spine, axis) * _global_rotation(r_global[ik.lean[i]]), r_local);
                _refresh(p_skeleton, r_local, r_global, ik.lean[i]);
            }
            if (ik.head >= 0) {
                _set_global_rotation(p_skeleton, r_global, ik.head, angleAxis(angle * CharacterAnim::HEAD_FLINCH, axis) * _global_rotation(r_global[ik.head]), r_local);
                _refresh(p_skeleton, r_local, r_global, ik.head);
            }
        }

        const float balance = an.balance_arms;
        if (balance > 0.01f) {
            const vec3 hips = vec3(r_global[ik.pelvis][3]);
            for (int i = 0; i < 2; i++) {
                if (ik.upperarm[i] < 0) continue;
                const vec3 shoulder = vec3(r_global[ik.upperarm[i]][3]);
                const vec3 outward = right * (dot(shoulder - hips, right) >= 0.0f ? 1.0f : -1.0f);
                const quat raise = angleAxis(glm::radians(CharacterAnim::BALANCE_ARM_ANGLE) * balance, normalize(cross(vec3(0.0f, -1.0f, 0.0f), outward)));
                _set_global_rotation(p_skeleton, r_global, ik.upperarm[i], raise * _global_rotation(r_global[ik.upperarm[i]]), r_local);
                _refresh(p_skeleton, r_local, r_global, ik.upperarm[i]);
            }
        }

    }
    if (std::fabs(an.chest) > 0.01f) {
        const quat facing = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f));
        const vec3 right = facing * vec3(-1.0f, 0.0f, 0.0f);
        uint32_t spine = 0;
        for (int i = 0; i < 3; i++) spine += ik.lean[i] >= 0 ? 1u : 0u;
        if (spine > 0) {
            const quat part = angleAxis(-glm::radians(an.chest) / (float)spine, right);
            for (int i = 0; i < 3; i++) {
                if (ik.lean[i] < 0) continue;
                _set_global_rotation(p_skeleton, r_global, ik.lean[i], part * _global_rotation(r_global[ik.lean[i]]), r_local);
                _refresh(p_skeleton, r_local, r_global, ik.lean[i]);
            }
        }
        if (ik.head >= 0) {
            const quat counter = angleAxis(glm::radians(an.chest * CharacterAnim::HEAD_COUNTER), right);
            _set_global_rotation(p_skeleton, r_global, ik.head, counter * _global_rotation(r_global[ik.head]), r_local);
            _refresh(p_skeleton, r_local, r_global, ik.head);
        }
    }
    if (alpha <= BLEND_MIN_WEIGHT && diagonal <= BLEND_MIN_WEIGHT && !swaying && !air_legs && reach_foot < 0 && roll_foot < 0 && !sinking && !warping) return;
    if (ik.pelvis_offset != 0.0f || sinking) {
        const int32_t parent = p_skeleton.parents[ik.pelvis];
        const vec3 moved = vec3(r_global[ik.pelvis][3]) + vec3(0.0f, ik.pelvis_offset * alpha + an.sink, 0.0f);
        r_local[ik.pelvis].translation = parent >= 0 ? vec3(inverse(r_global[parent]) * vec4(moved, 1.0f)) : moved;
        _refresh(p_skeleton, r_local, r_global, ik.pelvis);
    }

    for (int i = 0; i < 2; i++) {
        const vec3 a = vec3(r_global[ik.thigh[i]][3]);
        const vec3 b = vec3(r_global[ik.calf[i]][3]);
        const vec3 c = vec3(r_global[ik.foot[i]][3]);
        const vec3 pelvis_position = vec3(r_global[ik.pelvis][3]);
        const vec3 inward(pelvis_position.x - c.x, 0.0f, pelvis_position.z - c.z);
        vec3 target = c + stride_warp[i] + ik.offset[i] * alpha + inward * diagonal - sway_shift[i] + air_shift[i];
        if (i == reach_foot) target.y += (ik.rest_height[i] - c.y) * reach_weight;
        const float upper = length(b - a);
        const float lower = length(c - b);
        if (upper < 1.0e-4f || lower < 1.0e-4f) continue;

        const vec3 to_target = target - a;
        const float reach = std::clamp(length(to_target), std::fabs(upper - lower) + 1.0e-3f, upper + lower - 1.0e-3f);
        const vec3 dir = length(to_target) > 1.0e-5f ? normalize(to_target) : normalize(c - a);
        const vec3 ahead = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
        const vec3 lateral = normalize(cross(vec3(0.0f, 1.0f, 0.0f), ahead));
        vec3 bend = (b - a) - dir * dot(b - a, dir);
        bend -= lateral * (dot(bend, lateral) * FootIK::KNEE_LATERAL_CUT);
        vec3 pole = ahead - dir * dot(ahead, dir);
        if (length(pole) > 1.0e-4f) bend += normalize(pole) * FootIK::KNEE_POLE;
        if (length(bend) < 1.0e-5f) bend = cross(dir, lateral);
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
        quat foot_new = tilt * foot_old;
        const float brace = i == reach_foot ? reach_weight : (i == roll_foot ? roll_weight : 0.0f);
        if (brace > BLEND_MIN_WEIGHT) {
            const vec3 toe = foot_new * ik.toe_local[i];
            const vec3 flat(toe.x, 0.0f, toe.z);
            if (length(flat) > 1.0e-3f) {
                const vec3 along = normalize(flat);
                const vec3 normal = inverse(angleAxis(glm::radians(r_character.yaw + r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f))) * r_character.land_normal;
                const float slope = std::atan2(-dot(normal, along), std::max(normal.y, 0.1f));
                const float toe_pitch = std::asin(std::clamp(toe.y / length(toe), -1.0f, 1.0f));
                const float delta = (glm::radians(CharacterAnim::BRACE_TOE_UP) + slope - toe_pitch) * brace;
                foot_new = angleAxis(delta, normalize(cross(along, vec3(0.0f, 1.0f, 0.0f)))) * foot_new;
            }
        }

        _set_global_rotation(p_skeleton, r_global, ik.thigh[i], thigh_new, r_local);
        _refresh(p_skeleton, r_local, r_global, ik.thigh[i]);
        _set_global_rotation(p_skeleton, r_global, ik.calf[i], calf_new, r_local);
        _refresh(p_skeleton, r_local, r_global, ik.calf[i]);
        _set_global_rotation(p_skeleton, r_global, ik.foot[i], foot_new, r_local);
        _refresh(p_skeleton, r_local, r_global, ik.foot[i]);
    }
}

static void _air_start(CharacterAnim& r_anim, SideClip p_clip, bool p_loop, float p_rate, float p_fade)
{
    if (p_clip.clip == r_anim.air_current.clip && p_clip.mirror == r_anim.air_current.mirror && r_anim.air_current.clip.valid()) {
        r_anim.air_current.loop = p_loop;
        return;
    }
    r_anim.air_previous = r_anim.air_current;
    r_anim.air_current = AirClip{ p_clip.clip, p_clip.mirror, 0.0f, p_rate, p_loop };
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

static void _update_air(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, float p_dt)
{
    CharacterAnim& a = r_character.anim;
    const bool airborne = r_character.movement == MovementState::IN_AIR;
    const float speed = r_character.speed;

    if (r_character.jumped) {
        const bool left = r_character.jump_foot >= 0 ? r_character.jump_foot == 0 : a.stand_phase < 0.5f;
        const CharacterStance& stance = p_asset.standing;
        const bool walking = speed <= stance.walk_speed + CharacterAnim::JUMP_WALK_MARGIN;
        const SideClip walk = left ? side_clip(p_asset.jump_walk_left, p_asset.jump_walk_right) : side_clip(p_asset.jump_walk_right, p_asset.jump_walk_left);
        const SideClip run = left ? side_clip(p_asset.jump_left, p_asset.jump_right) : side_clip(p_asset.jump_right, p_asset.jump_left);
        const SideClip standing{ p_asset.jump_standing };
        SideClip clip;
        if (speed < CharacterAnim::JUMP_MOVING_SPEED) clip = standing;
        for (const SideClip& c : { walking ? walk : run, walking ? run : walk, standing }) {
            if (!clip.valid()) clip = c;
        }
        const float rate = CharacterAnim::JUMP_RATE_MIN + (CharacterAnim::JUMP_RATE_MAX - CharacterAnim::JUMP_RATE_MIN) * std::clamp(speed / CharacterAnim::JUMP_RATE_SPEED, 0.0f, 1.0f);
        a.land_time = -1.0f;
        a.jump_foot = speed >= CharacterAnim::JUMP_MOVING_SPEED ? (left ? 0 : 1) : -1;
        a.air_time = 0.0f;
        a.ready_time = -1.0f;
        a.roll_foot = -1;
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
            }
        }
    }

    if (r_character.landed) {
        const bool was_air = a.air_state != AirState::GROUNDED;
        a.air_state = AirState::GROUNDED;
        a.land_heavy = r_character.land_speed >= p_asset.heavy_land_speed && r_character.has_input && p_asset.land_heavy.valid();
        a.land_clip = a.land_heavy ? p_asset.land_heavy : Guid{};
        a.land_foot = was_air && a.jump_foot >= 0 ? (int8_t)(1 - a.jump_foot) : -1;
        a.land_roll = was_air && a.roll_foot >= 0 ? 1.0f : 0.0f;
        if (was_air && r_character.land_speed >= CharacterAnim::LAND_MIN_SPEED) {
            a.sink_velocity -= std::min(r_character.land_speed * CharacterAnim::LAND_SINK_SCALE, CharacterAnim::LAND_SINK_MAX);
            a.chest_velocity += std::min(r_character.land_speed * CharacterAnim::CHEST_SCALE, CharacterAnim::CHEST_MAX);
        }
        if (a.land_heavy) a.inert_request = true;
        a.jump_foot = -1;
        a.land_time = was_air && a.land_heavy ? 0.0f : -1.0f;
        a.land_strength = 1.0f;
    }
    if (!airborne && a.air_state != AirState::GROUNDED) a.air_state = AirState::GROUNDED;

    if (a.air_state == AirState::TAKEOFF && a.ready_time > 0.0f) {
        AirClip& held = a.air_current;
        const float left = std::max(a.ready_time - held.time, 0.0f);
        float rate = held.rate * std::clamp(left / (held.rate * CharacterAnim::READY_EASE), CharacterAnim::READY_MIN_RATE, 1.0f);
        if (r_character.land_known) rate = std::max(rate, std::min(left / std::max(r_character.land_eta * CharacterAnim::READY_SHARE, p_dt), held.rate * CharacterAnim::READY_MAX_SPEEDUP));
        held.time = std::min(held.time + p_dt * rate, a.ready_time);
    } else {
        _air_advance(a.air_current, r_assets, p_dt);
    }
    _air_advance(a.air_previous, r_assets, p_dt);
    a.air_fade = std::min(a.air_fade + p_dt / std::max(a.air_fade_time, 1.0e-3f), 1.0f);

    const bool show = a.air_state == AirState::TAKEOFF && a.air_current.clip.valid();
    const float shown = show ? 1.0f : 0.0f;
    if (a.started && shown != a.air_weight) a.inert_request = true;
    a.air_weight = shown;
    const float proc = a.air_state != AirState::GROUNDED ? 1.0f : 0.0f;
    a.air_proc = a.started ? _interp(a.air_proc, proc, p_dt, CharacterAnim::AIR_INTERP) : proc;
    if (!show && a.air_weight <= BLEND_MIN_WEIGHT) {
        a.air_current = AirClip{};
        a.air_previous = AirClip{};
    }

    const float vy = r_character.velocity.y;
    const float jump = std::max(p_asset.jump_speed, 1.0f);
    const bool jumping = a.air_state == AirState::TAKEOFF;
    const bool falling = a.air_state == AirState::FALL;
    const float flail_start = jumping ? jump + CharacterAnim::FLAIL_START_SPEED : CharacterAnim::FLAIL_START_SPEED;
    const float flail = jumping || falling ? std::clamp((-vy - flail_start) / (CharacterAnim::FLAIL_FULL_SPEED - CharacterAnim::FLAIL_START_SPEED), 0.0f, 1.0f) : 0.0f;
    float arms = 0.0f;
    if (falling) arms = CharacterAnim::AIR_ARM_FALL * (0.5f + 0.5f * flail);
    else if (jumping) arms = CharacterAnim::AIR_ARM_FALL * (0.5f + 0.5f * flail) * flail;
    a.land_roll = std::max(a.land_roll - p_dt / CharacterAnim::LAND_ROLL_TIME, 0.0f);
    if (a.land_roll <= 0.0f && !airborne) a.roll_foot = -1;
    {
        const float omega = CharacterAnim::LAND_SINK_SPRING;
        const float f = 1.0f + 2.0f * p_dt * omega;
        const float hoo = p_dt * omega * omega;
        const float inv = 1.0f / (f + p_dt * hoo);
        const float x = f * a.sink + p_dt * a.sink_velocity;
        const float v = a.sink_velocity - hoo * a.sink;
        a.sink = x * inv;
        a.sink_velocity = v * inv;
    }
    {
        const float omega = CharacterAnim::CHEST_SPRING;
        const float f = 1.0f + 2.0f * p_dt * omega;
        const float hoo = p_dt * omega * omega;
        const float inv = 1.0f / (f + p_dt * hoo);
        const float x = f * a.chest + p_dt * a.chest_velocity;
        const float v = a.chest_velocity - hoo * a.chest;
        a.chest = x * inv;
        a.chest_velocity = v * inv;
    }
    a.air_time += p_dt;
    a.air_arms = _interp(a.air_arms, arms, p_dt, CharacterAnim::AIR_POSE_INTERP);
    a.air_flail = _interp(a.air_flail, flail, p_dt, CharacterAnim::AIR_POSE_INTERP);
}

static vec3 _quat_log(quat p_q)
{
    p_q = normalize(p_q);
    if (p_q.w < 0.0f) p_q = -p_q;
    const vec3 v(p_q.x, p_q.y, p_q.z);
    const float s = length(v);
    if (s < 1.0e-6f) return v * 2.0f;
    return v / s * (2.0f * std::atan2(s, p_q.w));
}

static quat _quat_exp(const vec3& p_v)
{
    const float angle = length(p_v);
    if (angle < 1.0e-6f) return normalize(quat(1.0f, p_v.x * 0.5f, p_v.y * 0.5f, p_v.z * 0.5f));
    return angleAxis(angle, p_v / angle);
}

static void _decay(vec3& r_value, vec3& r_velocity, float p_dt)
{
    const float omega = CharacterAnim::INERT_SPRING;
    const vec3 j1 = r_velocity + r_value * omega;
    const float e = std::exp(-omega * p_dt);
    r_value = e * (r_value + j1 * p_dt);
    r_velocity = e * (r_velocity - j1 * omega * p_dt);
}

static void _inertialize(CharacterAnim& r_anim, uint32_t p_joints, float p_dt, JointTransform* r_pose)
{
    CharacterAnim& a = r_anim;
    if (a._inert_prev.size() != p_joints || p_dt <= 0.0f) {
        a._inert_prev.assign(r_pose, r_pose + p_joints);
        a._inert_prev2 = a._inert_prev;
        a._inert_rot.assign(p_joints, vec3(0.0f));
        a._inert_rot_velocity.assign(p_joints, vec3(0.0f));
        a._inert_pos.assign(p_joints, vec3(0.0f));
        a._inert_pos_velocity.assign(p_joints, vec3(0.0f));
        a.inert_request = false;
        a.inert_active = false;
        a.inert_dt = p_dt;
        return;
    }
    if (a.inert_request) {
        const float dt = std::max(a.inert_dt, 1.0e-4f);
        for (uint32_t j = 0; j < p_joints; j++) {
            a._inert_rot[j] = _quat_log(a._inert_prev[j].rotation * inverse(r_pose[j].rotation));
            a._inert_rot_velocity[j] = _quat_log(a._inert_prev[j].rotation * inverse(a._inert_prev2[j].rotation)) / dt;
            a._inert_pos[j] = a._inert_prev[j].translation - r_pose[j].translation;
            a._inert_pos_velocity[j] = (a._inert_prev[j].translation - a._inert_prev2[j].translation) / dt;
        }
        a.inert_active = true;
        a.inert_request = false;
    }
    if (a.inert_active) {
        float largest = 0.0f;
        for (uint32_t j = 0; j < p_joints; j++) {
            _decay(a._inert_rot[j], a._inert_rot_velocity[j], p_dt);
            _decay(a._inert_pos[j], a._inert_pos_velocity[j], p_dt);
            r_pose[j].rotation = normalize(_quat_exp(a._inert_rot[j]) * r_pose[j].rotation);
            r_pose[j].translation += a._inert_pos[j];
            largest = std::max({ largest, length(a._inert_rot[j]), length(a._inert_pos[j]) });
        }
        if (largest < CharacterAnim::INERT_DONE) a.inert_active = false;
    }
    a._inert_prev2 = a._inert_prev;
    a._inert_prev.assign(r_pose, r_pose + p_joints);
    a.inert_dt = p_dt;
}

static float _wrap01(float p_value)
{
    return p_value - std::floor(p_value);
}

static bool _contact_phases(const CharacterStance& p_stance, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float* r_phases)
{
    CharacterAnim& a = r_character.anim;
    const FootIK& ik = r_character.ik;
    const LBlendSpace* space = r_assets.blend_space(p_stance.cycles[(uint32_t)CycleDirection::FORWARD]);
    if (!space || !ik.valid || ik.foot[0] < 0 || ik.foot[1] < 0) return false;

    const float value = _mapped_speed(p_stance, std::max(r_character.speed, p_stance.walk_speed));
    const vec3 forward = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
    a._turn_globals.resize(p_skeleton.joint_count());
    float best[2] = { -1.0e9f, -1.0e9f };
    for (uint32_t s = 0; s < CharacterAnim::LAND_SYNC_SAMPLES; s++) {
        const float p = (float)s / (float)CharacterAnim::LAND_SYNC_SAMPLES;
        a._scratch_pose = p_skeleton.rest;
        if (!r_evaluator.blend_sample(r_assets, p_skeleton, *space, value, p, a._scratch_pose.data())) return false;
        p_skeleton.globals(a._scratch_pose.data(), a._turn_globals.data());
        for (int i = 0; i < 2; i++) {
            const float along = dot(vec3(a._turn_globals[ik.foot[i]][3]), forward);
            if (along <= best[i]) continue;
            best[i] = along;
            r_phases[i] = p;
        }
    }
    return true;
}

static float _ready_time(Character& r_character, ClipProvider& r_assets, const LSkeleton& p_skeleton)
{
    CharacterAnim& a = r_character.anim;
    const FootIK& ik = r_character.ik;
    const BoundClip clip = r_assets.clip(a.air_current.clip);
    if (!clip.valid() || clip.clip->duration <= 0.0f) return 0.0f;
    if (!ik.valid || a.jump_foot < 0) return clip.clip->duration;
    const int lead = 1 - a.jump_foot;
    const vec3 forward = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(0.0f, 0.0f, 1.0f);
    a._turn_globals.resize(p_skeleton.joint_count());
    float best = -1.0e9f;
    float time = clip.clip->duration;
    for (uint32_t s = 0; s < CharacterAnim::TURN_CURVE_SAMPLES; s++) {
        const float t = clip.clip->duration * (float)s / (float)(CharacterAnim::TURN_CURVE_SAMPLES - 1);
        a._scratch_pose = p_skeleton.rest;
        _sample(clip, t, a.air_current.mirror, a, p_skeleton, a._scratch_pose.data());
        p_skeleton.globals(a._scratch_pose.data(), a._turn_globals.data());
        const float along = dot(vec3(a._turn_globals[ik.foot[lead]][3]) - vec3(a._turn_globals[ik.pelvis][3]), forward);
        if (along <= best) continue;
        best = along;
        time = t;
    }
    return time;
}

static void _land_sync(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton)
{
    CharacterAnim& a = r_character.anim;
    const bool crouching = r_character.stance == Stance::CROUCHING;
    const CharacterStance& stance = crouching ? p_asset.crouching : p_asset.standing;
    float phases[2] = { 0.0f, 0.5f };
    _contact_phases(stance, r_character, r_assets, r_evaluator, p_skeleton, phases);
    (crouching ? a.crouch_phase : a.stand_phase) = phases[a.land_foot];
}

static void _jump_timing(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton)
{
    CharacterAnim& a = r_character.anim;
    a.jump_rush = 1.0f;
    if (!r_character.jump_wait) {
        a.contact_known = false;
        a.jump_target = -1;
        return;
    }
    if (a.jump_ready >= 0) return;
    if (!a.contact_known) {
        a.contact_known = true;
        a.contact_phase[0] = 0.0f;
        a.contact_phase[1] = 0.5f;
        _contact_phases(p_asset.standing, r_character, r_assets, r_evaluator, p_skeleton, a.contact_phase);
        a.jump_target = -1;
        const float since[2] = { _wrap01(a.stand_phase - a.contact_phase[0]), _wrap01(a.stand_phase - a.contact_phase[1]) };
        const int recent = since[0] <= since[1] ? 0 : 1;
        if (since[recent] <= CharacterAnim::JUMP_RECENT_PHASE) {
            a.jump_ready = (int8_t)recent;
            return;
        }
        a.jump_target = _wrap01(a.contact_phase[0] - a.stand_phase) <= _wrap01(a.contact_phase[1] - a.stand_phase) ? 0 : 1;
    }
    a.jump_distance = _wrap01(a.contact_phase[a.jump_target] - a.stand_phase);
    const float time = a.stand_phase_speed > 1.0e-3f ? a.jump_distance / a.stand_phase_speed : 0.0f;
    const float window = std::max(CharacterAnim::JUMP_WAIT_MAX - r_character.jump_wait_time, 0.03f);
    a.jump_rush = std::clamp(time / window, 1.0f, CharacterAnim::JUMP_RUSH_MAX);
}

bool character_pose(const LLocomotion& p_asset, Character& r_character, ClipProvider& r_assets, AnimEvaluator& r_evaluator, const LSkeleton& p_skeleton, float p_dt, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    if (joints == 0) return false;
    CharacterAnim& a = r_character.anim;
    a.mirror.bind(p_skeleton);
    const float speed = r_character.speed;
    const float direction = r_character.direction();

    const float crouch_target = r_character.stance == Stance::CROUCHING || r_character.sliding ? 1.0f : 0.0f;
    if (!a.started) {
        a.crouch_weight = crouch_target;
    } else {
        a.crouch_weight = _interp(a.crouch_weight, crouch_target, p_dt, CharacterAnim::STANCE_INTERP);
    }
    if (r_character.pivot) a.pivot_time = CharacterAnim::PIVOT_DURATION;
    a.pivot_time = std::max(a.pivot_time - p_dt, 0.0f);
    _update_air(p_asset, r_character, r_assets, p_dt);
    const float balance = std::clamp(std::max(length(r_character.sway) / CharacterRagdoll::SWAY_MAX, r_character.stagger_time > 0.0f ? CharacterAnim::STAGGER_ARM : 0.0f), 0.0f, 1.0f);
    a.balance_arms = a.started ? _interp(a.balance_arms, balance, p_dt, CharacterAnim::BALANCE_INTERP) : balance;
    if (a.air_state == AirState::TAKEOFF && a.ready_time < 0.0f) a.ready_time = _ready_time(r_character, r_assets, p_skeleton);
    if (a.land_foot >= 0) {
        _land_sync(p_asset, r_character, r_assets, r_evaluator, p_skeleton);
        a.land_step = r_character.speed >= CharacterAnim::JUMP_MOVING_SPEED ? 0.5f : 0.0f;
        a.land_foot = -1;
    }
    _jump_timing(p_asset, r_character, r_assets, r_evaluator, p_skeleton);
    if (a.land_step > 0.0f && (r_character.jump_wait || r_character.movement == MovementState::IN_AIR)) a.land_step = 0.0f;
    if (a.land_step > 0.0f) a.jump_rush = std::max(a.jump_rush, CharacterAnim::LAND_STEP_RUSH);
    const float phase_before = a.stand_phase;
    const float freeze = a.air_state == AirState::FALL || r_character.sliding ? 0.0f : 1.0f;

    bool crossing = true;
    if (r_character.ik.valid) {
        const vec3 right = angleAxis(glm::radians(-r_character.model_yaw), vec3(0.0f, 1.0f, 0.0f)) * vec3(-1.0f, 0.0f, 0.0f);
        crossing = std::fabs(dot(r_character.ik.model[0] - r_character.ik.model[1], right)) < CharacterAnim::HIPS_CROSS_DISTANCE;
    }
    _update_directions(a, direction, r_character.moving, speed, p_dt, !a.started || p_dt <= 0.0f, crossing);
    a.idle_time += p_dt;
    a._stand_pose = p_skeleton.rest;
    a._crouch_pose = p_skeleton.rest;
    const bool stand = a.crouch_weight < 1.0f - BLEND_MIN_WEIGHT && _stance_pose(p_asset.standing, a, a.stand_phase, a.stand_phase_speed, a.stand_stride, a.jump_rush * freeze, CharacterAnim::STANDING_MAX_RATE, r_assets, r_evaluator, p_skeleton, speed, p_dt, a._stand_pose.data());
    const bool crouch = a.crouch_weight > BLEND_MIN_WEIGHT && _stance_pose(p_asset.crouching, a, a.crouch_phase, a.crouch_phase_speed, a.crouch_stride, freeze, CharacterAnim::CROUCHING_MAX_RATE, r_assets, r_evaluator, p_skeleton, speed, p_dt, a._crouch_pose.data());
    a.started = true;
    if (r_character.jump_wait && a.jump_ready < 0 && a.jump_target >= 0 && _wrap01(a.stand_phase - phase_before) >= a.jump_distance) a.jump_ready = a.jump_target;
    if (a.land_step > 0.0f) a.land_step = std::max(a.land_step - _wrap01(a.stand_phase - phase_before), 0.0f);
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
    for (const CharacterAnim::FadingClip& f : a.transition_fading) posed = posed || f.weight > BLEND_MIN_WEIGHT;
    _turn_in_place(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed, transitioning);
    posed = posed || a.turn_weight > BLEND_MIN_WEIGHT;
    _rotate_in_place(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed, transitioning);
    posed = posed || a.rotate_weight > BLEND_MIN_WEIGHT;
    _stop_and_step(p_asset, r_character, r_assets, p_skeleton, p_dt, r_pose, posed);

    if (a.air_weight > BLEND_MIN_WEIGHT) {
        const BoundClip current = r_assets.clip(a.air_current.clip);
        if (current.valid() && current.clip->duration > 0.0f) {
            a._air_pose = p_skeleton.rest;
            _sample(current, a.air_current.time, a.air_current.mirror, a, p_skeleton, a._air_pose.data());
            const BoundClip previous = r_assets.clip(a.air_previous.clip);
            if (a.air_fade < 1.0f && previous.valid() && previous.clip->duration > 0.0f) {
                a._air_previous_pose = p_skeleton.rest;
                _sample(previous, a.air_previous.time, a.air_previous.mirror, a, p_skeleton, a._air_previous_pose.data());
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
            clip_sample(land, a.land_time, a._land_pose.data());
            if (posed) _mix(r_pose, a._land_pose.data(), weight, joints);
            else std::copy(a._land_pose.begin(), a._land_pose.end(), r_pose);
            posed = true;
            a.land_time += p_dt;
        }
    }
    if (r_character.action.layer(r_assets, p_skeleton, a._scratch_pose, r_pose, posed)) posed = true;
    if (posed) _inertialize(a, joints, p_dt, r_pose);
    return posed;
}

}
