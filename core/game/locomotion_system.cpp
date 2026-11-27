#include <core/game/locomotion_system.h>
#include <core/animation/skinning.h>
#include <core/animation/two_bone_ik.h>
#include <core/base/hash.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace lumen {

static const vec3 LOCO_UP(0.0f, 1.0f, 0.0f);

static std::string _lower(const std::string& p_text)
{
    std::string out = p_text;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static void _mix_pose(std::vector<JointTransform>& r_pose, const std::vector<JointTransform>& p_other, float p_t)
{
    if (p_other.size() != r_pose.size()) return;
    for (size_t j = 0; j < r_pose.size(); ++j) {
        r_pose[j].translation = mix(r_pose[j].translation, p_other[j].translation, p_t);
        r_pose[j].scale = mix(r_pose[j].scale, p_other[j].scale, p_t);
        r_pose[j].rotation = normalize(slerp(r_pose[j].rotation, p_other[j].rotation, p_t));
    }
}

static float _wrap_angle(float p_radians)
{
    p_radians = std::fmod(p_radians + glm::pi<float>(), glm::two_pi<float>());
    if (p_radians < 0.0f) p_radians += glm::two_pi<float>();
    return p_radians - glm::pi<float>();
}

static float _fract(float p_v)
{
    return p_v - std::floor(p_v);
}

static quat _rotation_of(const mat4& p_m)
{
    mat3 r(p_m);
    for (int c = 0; c < 3; ++c) {
        const float len = length(r[c]);
        if (len > 1e-6f) r[c] /= len;
    }
    return normalize(quat_cast(r));
}

static JointTransform _decompose(const mat4& p_m)
{
    JointTransform t;
    t.translation = vec3(p_m[3]);
    t.scale = vec3(length(vec3(p_m[0])), length(vec3(p_m[1])), length(vec3(p_m[2])));
    if (determinant(mat3(p_m)) < 0.0f) t.scale.x = -t.scale.x;
    mat3 rot(1.0f);
    for (int c = 0; c < 3; ++c) {
        if (t.scale[c] != 0.0f) rot[c] = vec3(p_m[c]) / t.scale[c];
    }
    t.rotation = normalize(quat_cast(rot));
    return t;
}

static mat4 _parent_global(const LSkeleton& p_skeleton, const std::vector<JointTransform>& p_local, int32_t p_joint)
{
    mat4 m(1.0f);
    for (int32_t j = p_skeleton.parents[p_joint]; j >= 0; j = p_skeleton.parents[j]) m = p_local[j].matrix() * m;
    return m;
}

static void _accumulate(std::vector<JointTransform>& r_acc, const std::vector<JointTransform>& p_pose, float p_weight, bool p_first)
{
    if (p_first) {
        for (size_t j = 0; j < p_pose.size(); ++j) {
            r_acc[j].translation = p_pose[j].translation * p_weight;
            r_acc[j].scale = p_pose[j].scale * p_weight;
            r_acc[j].rotation = p_pose[j].rotation * p_weight;
        }
        return;
    }
    for (size_t j = 0; j < p_pose.size(); ++j) {
        r_acc[j].translation += p_pose[j].translation * p_weight;
        r_acc[j].scale += p_pose[j].scale * p_weight;
        const quat q = dot(r_acc[j].rotation, p_pose[j].rotation) < 0.0f ? -p_pose[j].rotation : p_pose[j].rotation;
        r_acc[j].rotation = r_acc[j].rotation + q * p_weight;
    }
}

static void _normalize(std::vector<JointTransform>& r_pose, float p_total)
{
    const float inv = p_total > 1e-6f ? 1.0f / p_total : 1.0f;
    for (JointTransform& t : r_pose) {
        t.translation *= inv;
        t.scale *= inv;
        const float len = length(t.rotation);
        t.rotation = len > 1e-6f ? t.rotation / len : quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
}


void LocomotionSystem::start(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void LocomotionSystem::stop(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void LocomotionSystem::_clear()
{
    clips.clear();
    sets.clear();
    authored.clear();
    bindings.clear();
    characters.clear();
}

const LLocomotionSet* LocomotionSystem::_set(Guid p_guid)
{
    if (!p_guid.valid()) return nullptr;
    Set& set = sets[p_guid];
    const double now = ImGui::GetTime();
    if (now - set.poll < SET_POLL_S) return set.loaded ? &set.data : nullptr;
    set.poll = now;
    std::filesystem::path path;
    if (locate) path = locate(p_guid);
    else if (project) path = project->content_path(p_guid, LLocomotionSet::EXTENSION);
    if (path.empty()) {
        set.loaded = false;
        return nullptr;
    }
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(path, ec);
    if (ec) {
        set.loaded = false;
        return nullptr;
    }
    if (set.loaded && t == set.time) return &set.data;
    set.time = t;
    set.loaded = set.data.load(path) == Error::OK;
    if (!set.loaded) log_write("Locomotion: failed to load the locomotion set '%s'.", path.string().c_str());
    return set.loaded ? &set.data : nullptr;
}

const LocomotionSystem::Clip* LocomotionSystem::_clip(Guid p_guid)
{
    if (!p_guid.valid() || !project) return nullptr;
    auto [it, added] = clips.try_emplace(p_guid);
    Clip& clip = it->second;
    if (!added) return clip.loaded ? &clip : nullptr;
    clip.loaded = clip.anim.load_cooked(project->content_path(p_guid)) == Error::OK;
    if (!clip.loaded) {
        log_write("Locomotion: failed to load an animation. Reimport it if it was made before clip metadata.");
        return nullptr;
    }
    const std::vector<float>& phase = clip.anim.meta.phase;
    if (!phase.empty() && clip.anim.frame_count > 1) {
        uint32_t best = 0;
        float best_d = 2.0f;
        for (uint32_t f = 0; f < (uint32_t)phase.size(); ++f) {
            const float d = std::min(phase[f], 1.0f - phase[f]);
            if (d < best_d) {
                best_d = d;
                best = f;
            }
        }
        clip.cycle_offset = (float)best / (float)(clip.anim.frame_count - 1);
    }
    return &clip;
}

const LocomotionSystem::Binding* LocomotionSystem::_binding(Guid p_clip, const Clip& p_data, const LSkeleton& p_skeleton, Guid p_skeleton_guid)
{
    const uint64_t k = hash_combine(p_clip.value, p_skeleton_guid.value);
    auto [it, added] = bindings.try_emplace(k);
    Binding& b = it->second;
    if (!added) return &b;
    clip_track_map(p_skeleton, p_data.anim, b.map);
    const Guid source = p_data.anim.skeleton;
    if (project && source.valid() && source != p_skeleton_guid) {
        auto [sk, loaded] = authored.try_emplace(source);
        if (loaded && sk->second.load_cooked(project->content_path(source)) != Error::OK) sk->second = LSkeleton{};
        if (sk->second.joint_count()) clip_retarget(sk->second, p_skeleton, b.map, b.retarget);
    }
    if (!p_data.anim.meta.root_joint.empty()) {
        b.root_joint = p_skeleton.find(p_data.anim.meta.root_joint);
        const std::string lower = _lower(p_data.anim.meta.root_joint);
        b.root_bone = b.root_joint != SKELETON_NO_JOINT && lower.find("pelvis") == std::string::npos && lower.find("hips") == std::string::npos;
    }
    return &b;
}

const LocomotionSystem::Clip* LocomotionSystem::_slot_clip(const LLocomotionSet& p_set, LocoSlot p_slot)
{
    return _clip(p_set.clip(p_slot));
}

bool LocomotionSystem::_sample(const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_time, std::vector<JointTransform>& r_pose)
{
    r_pose = p_skeleton.rest;
    const Guid guid = p_set.clip(p_slot);
    const Clip* clip = _clip(guid);
    if (!clip) return false;
    const Binding* b = _binding(guid, *clip, p_skeleton, p_skeleton_guid);
    clip_sample(BoundClip{ &clip->anim, &b->map, &b->retarget }, p_time, r_pose.data());

    const ClipMeta& meta = clip->anim.meta;
    const bool one_shot = _one_shot(p_slot);
    const bool root_motion = one_shot || meta.has_root_motion() || std::abs(meta.turn) > ROOT_MOTION_TURN;
    if (!root_motion || b->root_joint == SKELETON_NO_JOINT || meta.trajectory.empty()) return true;
    const float scale = b->retarget.active() ? b->retarget.scale : 1.0f;
    const vec4 first = meta.trajectory.front();
    const vec4 now = clip->anim.trajectory_at(p_time);
    const bool travels = one_shot ? (b->root_bone || (p_slot == LocoSlot::ROLL && meta.has_root_motion())) : meta.has_root_motion();
    const vec3 travel = travels ? vec3(now.x - first.x, 0.0f, now.z - first.z) * scale : vec3(0.0f);
    const mat4 trajectory = translate(mat4(1.0f), travel) * mat4_cast(angleAxis(now.w, LOCO_UP));
    const int32_t j = b->root_joint;
    const mat4 parent = _parent_global(p_skeleton, r_pose, j);
    const mat4 global = parent * r_pose[j].matrix();
    r_pose[j] = _decompose(inverse(parent) * inverse(trajectory) * global);
    return true;
}

void LocomotionSystem::_legs(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton)
{
    if (r_char.legs_ready) return;
    r_char.legs_ready = true;
    static constexpr LocoSlot SOURCES[] = { LocoSlot::WALK_F, LocoSlot::RUN_F, LocoSlot::CROUCH_WALK_F, LocoSlot::SPRINT_F };
    for (LocoSlot slot : SOURCES) {
        const Clip* clip = _slot_clip(p_set, slot);
        if (!clip) continue;
        for (uint32_t side = 0; side < 2; ++side) {
            if (r_char.legs[side].valid() || clip->anim.meta.feet[side].joint.empty()) continue;
            r_char.legs[side] = two_bone_chain(p_skeleton, p_skeleton.find(clip->anim.meta.feet[side].joint));
        }
    }
    const TwoBoneChain& any = r_char.legs[0].valid() ? r_char.legs[0] : r_char.legs[1];
    r_char.pelvis = any.valid() ? p_skeleton.parents[any.upper] : SKELETON_NO_JOINT;
    if (r_char.pelvis != SKELETON_NO_JOINT) {
        r_char.top = r_char.pelvis;
        while (p_skeleton.parents[r_char.top] >= 0) r_char.top = p_skeleton.parents[r_char.top];
    }
}

void LocomotionSystem::_stride_warp(Character& r_char, const LSkeleton& p_skeleton, std::vector<JointTransform>& r_pose, float p_stride, const vec3& p_dir)
{
    _globals.resize(p_skeleton.joint_count());
    p_skeleton.globals(r_pose.data(), _globals.data());
    vec3 hips(0.0f);
    if (r_char.pelvis != SKELETON_NO_JOINT) {
        hips = vec3(_globals[r_char.pelvis][3]);
    } else {
        for (const TwoBoneChain& leg : r_char.legs) {
            if (leg.valid()) hips = vec3(_globals[leg.upper][3]);
        }
    }
    vec3 feet[2];
    float low = 1.0e9f;
    for (uint32_t side = 0; side < 2; ++side) {
        if (!r_char.legs[side].valid()) continue;
        feet[side] = vec3(_globals[r_char.legs[side].end][3]);
        low = std::min(low, feet[side].y);
    }
    const float lift = STRIDE_LIFT + (1.0f - STRIDE_LIFT) * p_stride;
    for (uint32_t side = 0; side < 2; ++side) {
        if (!r_char.legs[side].valid()) continue;
        vec3 target = feet[side] - p_dir * dot(feet[side] - hips, p_dir) * (1.0f - p_stride);
        target.y = low + (feet[side].y - low) * lift;
        two_bone_ik(p_skeleton, r_pose, _globals, r_char.legs[side], target, vec3(0.0f, 0.0f, 1.0f), true);
    }
}


void LocomotionSystem::_stance_sample(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, LocoSlot p_slot, float p_time, bool p_normalized, std::vector<JointTransform>& r_pose)
{
    const Clip* clip = _slot_clip(p_set, p_slot);
    if (!clip || !_sample(p_set, p_slot, p_skeleton, p_skeleton_guid, p_normalized ? p_time * clip->anim.duration : p_time, r_pose)) {
        r_pose = p_skeleton.rest;
        return;
    }
    const LocoSlot stand = loco_stand_slot(p_slot);
    const LocoSlot crouch = loco_crouch_slot(stand);
    if (stand == crouch) return;
    const LocoSlot other = p_slot == crouch ? stand : crouch;
    const float weight = p_slot == crouch ? 1.0f - r_char.stance : r_char.stance;
    const Clip* pair = _slot_clip(p_set, other);
    if (weight <= 1e-3f || !pair || pair->anim.duration <= 0.0f) return;
    const float t = p_normalized ? p_time * pair->anim.duration : std::fmod(p_time, pair->anim.duration);
    if (_sample(p_set, other, p_skeleton, p_skeleton_guid, t, r_char.stance_pose)) _mix_pose(r_pose, r_char.stance_pose, weight);
}

void LocomotionSystem::_effort(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid)
{
    if (r_char.impulse <= 1e-3f || r_char.arms.empty() || _slot_clip(p_set, LocoSlot::SPRINT_IMPULSE)) return;
    if (r_char.swing_mean.size() != r_char.out.size()) {
        const Clip* sprint = _slot_clip(p_set, LocoSlot::SPRINT_F);
        if (!sprint) sprint = _slot_clip(p_set, LocoSlot::RUN_F);
        if (!sprint) return;
        const LocoSlot slot = _slot_clip(p_set, LocoSlot::SPRINT_F) ? LocoSlot::SPRINT_F : LocoSlot::RUN_F;
        std::vector<JointTransform> mean = p_skeleton.rest;
        for (uint32_t i = 0; i < SWING_SAMPLES; ++i) {
            _sample(p_set, slot, p_skeleton, p_skeleton_guid, sprint->anim.duration * (float)i / (float)SWING_SAMPLES, r_char.scratch);
            _accumulate(mean, r_char.scratch, 1.0f, i == 0);
        }
        _normalize(mean, (float)SWING_SAMPLES);
        r_char.swing_mean.resize(mean.size());
        for (size_t j = 0; j < mean.size(); ++j) r_char.swing_mean[j] = mean[j].rotation;
    }
    const float gain = r_char.impulse * IMPULSE_ARMS;
    for (int32_t j : r_char.arms) {
        quat delta = r_char.out[j].rotation * inverse(r_char.swing_mean[j]);
        if (delta.w < 0.0f) delta = -delta;
        const float angle = 2.0f * std::acos(std::clamp(delta.w, -1.0f, 1.0f));
        const float s = std::sqrt(std::max(1.0f - delta.w * delta.w, 0.0f));
        if (angle < 1e-4f || s < 1e-6f) continue;
        const vec3 axis(delta.x / s, delta.y / s, delta.z / s);
        r_char.out[j].rotation = normalize(angleAxis(angle * (1.0f + gain), axis) * r_char.swing_mean[j]);
    }
}

const std::vector<vec3>* LocomotionSystem::_mantle_path(Character& r_char, const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid)
{
    const Guid guid = p_set.clip(p_slot);
    auto it = clips.find(guid);
    if (it == clips.end() || !it->second.loaded) return nullptr;
    Clip& clip = it->second;
    _legs(r_char, p_set, p_skeleton);
    int32_t hips = r_char.pelvis;
    const Binding* b = _binding(guid, clip, p_skeleton, p_skeleton_guid);
    if (hips == SKELETON_NO_JOINT) hips = b->root_joint;
    if (hips == SKELETON_NO_JOINT) return nullptr;
    r_char.top = hips;
    while (p_skeleton.parents[r_char.top] >= 0) r_char.top = p_skeleton.parents[r_char.top];
    if (clip.path_skeleton == p_skeleton_guid && !clip.path.empty()) return &clip.path;
    const uint32_t frames = std::max(clip.anim.frame_count, 2u);
    clip.path.resize(frames);
    std::vector<JointTransform> pose;
    _globals.resize(p_skeleton.joint_count());
    vec3 first(0.0f);
    for (uint32_t f = 0; f < frames; ++f) {
        pose = p_skeleton.rest;
        clip_sample(BoundClip{ &clip.anim, &b->map, &b->retarget }, clip.anim.duration * (float)f / (float)(frames - 1), pose.data());
        p_skeleton.globals(pose.data(), _globals.data());
        const vec3 at = vec3(_globals[hips][3]);
        if (f == 0) first = at;
        clip.path[f] = at - first;
    }
    clip.path_skeleton = p_skeleton_guid;
    return &clip.path;
}

void LocomotionSystem::_dip(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, float p_amount)
{
    _legs(r_char, p_set, p_skeleton);
    if (r_char.pelvis == SKELETON_NO_JOINT || p_amount <= 1e-4f) return;
    _globals.resize(p_skeleton.joint_count());
    p_skeleton.globals(r_char.out.data(), _globals.data());
    vec3 feet[2];
    float leg = 0.0f;
    for (uint32_t side = 0; side < 2; ++side) {
        if (!r_char.legs[side].valid()) continue;
        feet[side] = vec3(_globals[r_char.legs[side].end][3]);
        leg = std::max(leg, length(vec3(_globals[r_char.legs[side].upper][3]) - feet[side]));
    }
    if (leg <= 0.0f) return;
    const int32_t parent = p_skeleton.parents[r_char.pelvis];
    const mat3 to_parent = parent >= 0 ? inverse(mat3(_globals[parent])) : mat3(1.0f);
    r_char.out[r_char.pelvis].translation += to_parent * vec3(0.0f, -leg * DIP_DEPTH * p_amount, 0.0f);
    for (uint32_t side = 0; side < 2; ++side) {
        if (r_char.legs[side].valid()) two_bone_ik(p_skeleton, r_char.out, _globals, r_char.legs[side], feet[side], vec3(0.0f, 0.0f, 1.0f), true);
    }
}

bool LocomotionSystem::_one_shot(LocoSlot p_slot)
{
    const LocoSlot stand = loco_stand_slot(p_slot);
    return (stand >= LocoSlot::TURN_L90 && stand <= LocoSlot::ROTATE_R) || p_slot == LocoSlot::CROUCH_ENTER || p_slot == LocoSlot::CROUCH_EXIT || p_slot >= LocoSlot::ROLL;
}

vec3 LocomotionSystem::_travel(const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_time)
{
    const Guid guid = p_set.clip(p_slot);
    const Clip* clip = _clip(guid);
    if (!clip || clip->anim.meta.trajectory.empty()) return vec3(0.0f);
    const Binding* b = _binding(guid, *clip, p_skeleton, p_skeleton_guid);
    if (!b->root_bone && !(p_slot == LocoSlot::ROLL && clip->anim.meta.has_root_motion())) return vec3(0.0f);
    const float scale = b->retarget.active() ? b->retarget.scale : 1.0f;
    const vec4 first = clip->anim.meta.trajectory.front();
    const vec4 now = clip->anim.trajectory_at(p_time);
    return vec3(now.x - first.x, 0.0f, now.z - first.z) * scale;
}

void LocomotionSystem::_push_root(MovementComponent* r_move, const Clip& p_clip, const vec3& p_model_delta, float p_time)
{
    if (!r_move) return;
    const float base = r_move->facing - p_clip.anim.trajectory_at(p_time).w;
    r_move->root_motion += angleAxis(base, LOCO_UP) * p_model_delta;
}

void LocomotionSystem::_switch(Character& r_char, State p_state, LocoSlot p_slot, const LLocomotionSet& p_set)
{
    if (r_char.state == p_state && r_char.slot == p_slot) return;
    r_char.state = p_state;
    r_char.slot = p_slot;
    r_char.time = 0.0f;
    r_char.aligning = false;
    r_char.turn_wait = 0.0f;
    r_char.blend_pending = !r_char.last.empty();
    r_char.blend_time = std::max(p_set.blend_time, 0.0f);
}

void LocomotionSystem::_blend(Character& r_char, float p_dt)
{
    const size_t count = r_char.out.size();
    if (r_char.blend_pending && r_char.sampled == r_char.state) {
        r_char.blend_pending = false;
        if (r_char.last.size() == count && r_char.blend_time > 0.0f) {
            r_char.blend_rot.resize(count);
            r_char.blend_pos.resize(count);
            for (size_t j = 0; j < count; ++j) {
                quat q = r_char.last[j].rotation * inverse(r_char.out[j].rotation);
                if (q.w < 0.0f) q = -q;
                r_char.blend_rot[j] = q;
                r_char.blend_pos[j] = r_char.last[j].translation - r_char.out[j].translation;
            }
            r_char.blend_elapsed = 0.0f;
        } else {
            r_char.blend_rot.clear();
        }
    } else if (!r_char.blend_pending) {
        r_char.blend_elapsed += p_dt;
    }
    if (r_char.blend_rot.size() != count || r_char.blend_time <= 0.0f) return;
    const float x = std::clamp(r_char.blend_elapsed / r_char.blend_time, 0.0f, 1.0f);
    if (x >= 1.0f) {
        r_char.blend_rot.clear();
        return;
    }
    const float k = 1.0f - x * x * (3.0f - 2.0f * x);
    const quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    for (size_t j = 0; j < count; ++j) {
        r_char.out[j].rotation = normalize(slerp(identity, r_char.blend_rot[j], k) * r_char.out[j].rotation);
        r_char.out[j].translation += r_char.blend_pos[j] * k;
    }
}

bool LocomotionSystem::_cycle(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, const MovementComponent* p_move, float p_dt, bool p_crouch, bool p_advance, std::vector<JointTransform>& r_pose)
{
    struct Gait { LocoSlot f, b, lf, lb, rf, rb; float native; float weight; };
    Gait gaits[3] = {
        { LocoSlot::WALK_F, LocoSlot::WALK_B, LocoSlot::WALK_LF, LocoSlot::WALK_LB, LocoSlot::WALK_RF, LocoSlot::WALK_RB, p_move ? p_move->walk_speed : 1.75f, 0.0f },
        { LocoSlot::RUN_F, LocoSlot::RUN_B, LocoSlot::RUN_LF, LocoSlot::RUN_LB, LocoSlot::RUN_RF, LocoSlot::RUN_RB, p_move ? p_move->run_speed : 3.75f, 0.0f },
        { LocoSlot::SPRINT_F, LocoSlot::RUN_B, LocoSlot::RUN_LF, LocoSlot::RUN_LB, LocoSlot::RUN_RF, LocoSlot::RUN_RB, p_move ? p_move->sprint_speed : 6.0f, 0.0f },
    };
    if (p_crouch && _slot_clip(p_set, LocoSlot::CROUCH_WALK_F)) {
        gaits[0] = { LocoSlot::CROUCH_WALK_F, LocoSlot::CROUCH_WALK_B, LocoSlot::CROUCH_WALK_L, LocoSlot::CROUCH_WALK_L, LocoSlot::CROUCH_WALK_R, LocoSlot::CROUCH_WALK_R, p_move ? p_move->crouch_speed : 1.4f, 0.0f };
        gaits[1].f = LocoSlot::COUNT;
        gaits[2].f = LocoSlot::COUNT;
    }
    uint32_t available[3];
    uint32_t count = 0;
    for (uint32_t g = 0; g < 3; ++g) {
        if (gaits[g].f == LocoSlot::COUNT) continue;
        const Clip* forward = _slot_clip(p_set, gaits[g].f);
        if (!forward) continue;
        if (forward->anim.meta.speed > 0.1f) gaits[g].native = forward->anim.meta.speed;
        available[count++] = g;
    }
    r_pose = p_skeleton.rest;
    if (count == 0) return false;

    vec3 velocity = p_move ? vec3(p_move->velocity.x, 0.0f, p_move->velocity.z) : vec3(0.0f);
    float speed = length(velocity);
    if (speed < 0.05f && p_move) velocity = vec3(p_move->want_move.x, 0.0f, p_move->want_move.z);

    if (speed <= gaits[available[0]].native) {
        gaits[available[0]].weight = 1.0f;
    } else if (speed >= gaits[available[count - 1]].native) {
        gaits[available[count - 1]].weight = 1.0f;
    } else {
        for (uint32_t i = 0; i + 1 < count; ++i) {
            Gait& a = gaits[available[i]];
            Gait& b = gaits[available[i + 1]];
            if (speed < a.native || speed > b.native) continue;
            const float t = b.native > a.native ? (speed - a.native) / (b.native - a.native) : 0.0f;
            a.weight = 1.0f - t;
            b.weight = t;
            break;
        }
    }

    float target_angle = 0.0f;
    bool steer = true;
    if (!p_move || p_move->strafing) {
        const float facing = p_move ? p_move->facing : 0.0f;
        const vec3 forward(std::sin(facing), 0.0f, std::cos(facing));
        const vec3 right(-forward.z, 0.0f, forward.x);
        vec3 heading = velocity;
        if (p_move && speed < DIR_VELOCITY_MIN) heading = vec3(p_move->want_move.x, 0.0f, p_move->want_move.z);
        if (length(heading) > 1e-3f) target_angle = std::atan2(dot(heading, right), dot(heading, forward));
        else steer = false;
    }
    if (p_advance && steer) r_char.dir_angle = _wrap_angle(r_char.dir_angle + _wrap_angle(target_angle - r_char.dir_angle) * std::clamp(p_dt * DIR_SMOOTH, 0.0f, 1.0f));
    const float local_z = std::cos(r_char.dir_angle);
    const float local_x = std::sin(r_char.dir_angle);
    const bool want_back = local_z < -HIP_SWITCH ? true : local_z > HIP_SWITCH ? false : r_char.hips_back;
    const bool sideways = std::abs(local_x) > 0.1f;
    if (p_advance && want_back != r_char.hips_back && (r_char.foot_landed || !sideways)) {
        r_char.hips_back = want_back;
        if (sideways) {
            r_char.blend_pending = !r_char.last.empty();
            r_char.blend_time = std::max(p_set.blend_time, 0.0f);
        }
    }
    const bool hips_back = r_char.hips_back;
    if (p_advance) {
        const float accel = p_dt > 0.0f ? (speed - r_char.prev_speed) / p_dt : 0.0f;
        r_char.prev_speed = speed;
        float target = 0.0f;
        if (p_move && !p_crouch && p_move->want_gait == MoveGait::SPRINT && accel > IMPULSE_MIN_ACCEL) target = std::clamp(accel / std::max(p_move->acceleration, 0.1f), 0.0f, 1.0f);
        const float rate = target > r_char.impulse ? IMPULSE_RISE : IMPULSE_FALL;
        r_char.impulse += (target - r_char.impulse) * std::clamp(p_dt * rate, 0.0f, 1.0f);
    }
    const Clip* impulse = r_char.impulse > 1e-3f ? _slot_clip(p_set, LocoSlot::SPRINT_IMPULSE) : nullptr;
    const float dir_raw[4] = { std::max(local_z, 0.0f), std::max(-local_z, 0.0f), std::max(-local_x, 0.0f), std::max(local_x, 0.0f) };
    auto side = [&](LocoSlot p_a, LocoSlot p_b) {
        if (p_a != LocoSlot::COUNT && _slot_clip(p_set, p_a)) return p_a;
        if (p_b != LocoSlot::COUNT && _slot_clip(p_set, p_b)) return p_b;
        return LocoSlot::COUNT;
    };

    _legs(r_char, p_set, p_skeleton);
    const float ratio = p_move && gaits[available[0]].native > 1e-3f ? std::clamp(speed / gaits[available[0]].native, 0.0f, 1.0f) : 1.0f;
    const bool strided = ratio < 1.0f && (r_char.legs[0].valid() || r_char.legs[1].valid());
    const float stride_rate = strided ? std::clamp(std::sqrt(ratio), MIN_RATE, 1.0f) : 1.0f;
    const float stride = strided ? std::clamp(ratio / stride_rate, 0.0f, 1.0f) : 1.0f;

    struct Entry { LocoSlot slot; float weight; const Clip* clip; };
    Entry entries[12];
    uint32_t entry_count = 0;
    float native = 0.0f;
    float duration = 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        const Gait& gait = gaits[available[i]];
        if (gait.weight <= 0.0f) continue;
        native += gait.native * gait.weight;
        const LocoSlot dirs[4] = { gait.f, side(gait.b, LocoSlot::COUNT), hips_back ? side(gait.lb, gait.lf) : side(gait.lf, gait.lb), hips_back ? side(gait.rb, gait.rf) : side(gait.rf, gait.rb) };
        float dir[4] = { dir_raw[0], dir_raw[1], dir_raw[2], dir_raw[3] };
        for (uint32_t d = 1; d < 4; ++d) {
            if (dirs[d] != LocoSlot::COUNT) continue;
            dir[0] += dir[d];
            dir[d] = 0.0f;
        }
        const float sum = dir[0] + dir[1] + dir[2] + dir[3];
        for (uint32_t d = 0; d < 4; ++d) {
            if (dir[d] <= 0.0f) continue;
            const Clip* clip = _slot_clip(p_set, dirs[d]);
            const float w = gait.weight * dir[d] / std::max(sum, 1e-6f);
            entries[entry_count++] = { dirs[d], w, clip };
            duration += clip->anim.duration * w;
            total += w;
        }
    }
    if (entry_count == 0 || total <= 0.0f) return false;
    duration /= total;

    float rate = p_move && native > 1e-3f ? std::clamp(speed / native, MIN_RATE, MAX_RATE) : 1.0f;
    if (strided) rate = stride_rate;
    if (p_advance && duration > 1e-4f) r_char.cycle = _fract(r_char.cycle + p_dt * rate / duration);

    for (uint32_t i = 0; i < entry_count; ++i) {
        const Entry& e = entries[i];
        const float phase = _fract(r_char.cycle + e.clip->cycle_offset);
        _sample(p_set, e.slot, p_skeleton, p_skeleton_guid, phase * e.clip->anim.duration, r_char.scratch);
        if (e.slot == LocoSlot::SPRINT_F && impulse && _sample(p_set, LocoSlot::SPRINT_IMPULSE, p_skeleton, p_skeleton_guid, _fract(r_char.cycle + impulse->cycle_offset) * impulse->anim.duration, r_char.impulse_pose)) _mix_pose(r_char.scratch, r_char.impulse_pose, r_char.impulse);
        _accumulate(r_pose, r_char.scratch, e.weight, i == 0);
    }
    _normalize(r_pose, total);
    if (strided) {
        vec3 dir(-local_x, 0.0f, local_z);
        if (length(dir) > 1e-4f) _stride_warp(r_char, p_skeleton, r_pose, stride, normalize(dir));
    }
    if (!p_advance) return true;

    const Entry* dominant = &entries[0];
    for (uint32_t i = 1; i < entry_count; ++i) {
        if (entries[i].weight > dominant->weight) dominant = &entries[i];
    }
    r_char.time = _fract(r_char.cycle + dominant->clip->cycle_offset) * dominant->clip->anim.duration;
    r_char.slot = dominant->slot;

    const ClipMeta& meta = dominant->clip->anim.meta;
    const uint32_t frame = dominant->clip->anim.frame_at(r_char.time);
    bool tracked = false;
    bool landed = false;
    bool any_down = false;
    for (uint32_t foot = 0; foot < (uint32_t)ClipFootSide::COUNT; ++foot) {
        if (meta.feet[foot].contacts.empty()) continue;
        tracked = true;
        const bool down = meta.feet[foot].planted(frame);
        if (down && !r_char.foot_down[foot]) landed = true;
        r_char.foot_down[foot] = down;
        any_down = any_down || down;
    }
    r_char.foot_landed = !tracked || landed;
    r_char.foot_grip = !tracked || any_down;
    return true;
}

void LocomotionSystem::_lean(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, const MovementComponent* p_move, float p_dt)
{
    if (!p_move || p_dt <= 0.0f) return;

    const vec3 velocity(p_move->velocity.x, 0.0f, p_move->velocity.z);
    const float speed = length(velocity);
    vec2 target(0.0f);
    if (r_char.prev_ready && r_char.state == State::MOVE) {
        const vec3 forward(std::sin(p_move->facing), 0.0f, std::cos(p_move->facing));
        const vec3 right(-forward.z, 0.0f, forward.x);
        const vec3 accel = (velocity - r_char.prev_velocity) / p_dt;
        const float yaw_rate = _wrap_angle(p_move->facing - r_char.prev_facing) / p_dt;
        const float limit = std::max(p_set.lean_acceleration, 0.1f);
        target.x = std::clamp((dot(accel, right) - yaw_rate * speed) / limit, -1.0f, 1.0f);
        target.y = std::clamp(dot(accel, forward) / limit, -1.0f, 1.0f);
        target *= std::clamp(speed, 0.0f, 1.0f);
        target.y = std::min(target.y + r_char.impulse * IMPULSE_LEAN, 1.0f);
    }
    r_char.prev_velocity = velocity;
    r_char.prev_facing = p_move->facing;
    r_char.prev_ready = true;
    r_char.lean += (target - r_char.lean) * std::clamp(p_dt * LEAN_SMOOTH, 0.0f, 1.0f);

    if (r_char.lean_chain.empty() || length(r_char.lean) < 1e-4f) return;
    const float share = glm::radians(p_set.lean_angle) / (float)r_char.lean_chain.size();
    const quat tilt = angleAxis(r_char.lean.y * share, vec3(1.0f, 0.0f, 0.0f)) * angleAxis(r_char.lean.x * share, vec3(0.0f, 0.0f, 1.0f));
    _globals.resize(p_skeleton.joint_count());
    for (int32_t j : r_char.lean_chain) {
        p_skeleton.globals(r_char.out.data(), _globals.data());
        const int32_t parent = p_skeleton.parents[j];
        const quat parent_rot = parent >= 0 ? _rotation_of(_globals[parent]) : quat(1.0f, 0.0f, 0.0f, 0.0f);
        r_char.out[j].rotation = normalize(inverse(parent_rot) * tilt * _rotation_of(_globals[j]));
    }
}

static uint64_t _entity_key(Entity p_entity)
{
    return ((uint64_t)p_entity.index << 32) | p_entity.generation;
}

void LocomotionSystem::_evaluate(Scene& r_world, Entity p_entity, Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_dt)
{
    MovementComponent* move = r_world.try_get<MovementComponent>(p_entity);
    const vec3 velocity = move ? vec3(move->velocity.x, 0.0f, move->velocity.z) : vec3(0.0f);
    const float speed = length(velocity);
    const bool wants = move && length(vec2(move->want_move.x, move->want_move.z)) > 0.01f;
    const bool crouch = move && move->crouching;
    auto pick = [&](LocoSlot p_slot) {
        const LocoSlot c = loco_crouch_slot(p_slot);
        return crouch && c != p_slot && _slot_clip(p_set, c) ? c : p_slot;
    };

    if (crouch != r_char.crouched) {
        r_char.crouched = crouch;
        const LocoSlot change = crouch ? LocoSlot::CROUCH_ENTER : LocoSlot::CROUCH_EXIT;
        const Clip* clip = _slot_clip(p_set, change);
        r_char.stance_from = r_char.stance;
        r_char.stance_to = crouch ? 1.0f : 0.0f;
        r_char.stance_time = 0.0f;
        r_char.stance_x = 0.0f;
        r_char.stance_slot = clip ? change : LocoSlot::COUNT;
        r_char.stance_duration = clip && clip->anim.duration > 0.0f ? clip->anim.duration : std::max(p_set.blend_time, 0.01f);
        if ((r_char.state == State::IDLE || r_char.state == State::TRANSITION) && !wants && clip) _switch(r_char, State::TRANSITION, change, p_set);
    }
    if (r_char.stance_x < 1.0f) {
        float factor = 1.0f;
        if (move && wants && r_char.state != State::TRANSITION) {
            const float goal = move->crouching ? move->crouch_speed : move->gait_speed(move->want_gait);
            factor = 1.0f - (1.0f - STANCE_FASTEST) * std::clamp(goal / std::max(move->sprint_speed, 0.1f), 0.0f, 1.0f);
        }
        r_char.stance_x = std::min(r_char.stance_x + p_dt / std::max(r_char.stance_duration * factor, 1e-3f), 1.0f);
        r_char.stance_time = r_char.stance_x * r_char.stance_duration;
        const float x = r_char.stance_x;
        r_char.stance = r_char.stance_from + (r_char.stance_to - r_char.stance_from) * x * x * (3.0f - 2.0f * x);
    }

    if (move && move->want_roll) {
        move->want_roll = false;
        const Clip* roll = _slot_clip(p_set, LocoSlot::ROLL);
        if (roll && roll->anim.duration > 0.0f && r_char.state != State::ROLL && r_char.state != State::MANTLE && move->grounded) {
            const vec2 want(move->want_move.x, move->want_move.z);
            const float before = move->facing;
            if (length(want) > 0.01f) move->facing = std::atan2(want.x, want.y);
            if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) t->rotation = angleAxis(move->facing, LOCO_UP);
            r_char.yaw_blend = _wrap_angle(before - move->facing);
            r_char.yaw_elapsed = 0.0f;
            r_char.yaw_time = ROLL_BLEND_IN;
            const float fraction = std::clamp(speed / std::max(move->sprint_speed, 0.1f), 0.0f, 1.0f);
            r_char.roll_rate = ROLL_RATE_MIN + (ROLL_RATE_MAX - ROLL_RATE_MIN) * fraction;
            r_char.roll_velocity = vec3(0.0f);
            r_char.roll_entry = speed;
            _switch(r_char, State::ROLL, LocoSlot::ROLL, p_set);
            r_char.blend_time = std::max(p_set.blend_time, ROLL_BLEND_IN);
            move->rolling = true;
        }
    }

    if (move && move->mantle_request) {
        move->mantle_request = false;
        const bool left = r_char.foot_down[(int)ClipFootSide::LEFT];
        const LocoSlot near_low = left ? LocoSlot::MANTLE_1M_LH : LocoSlot::MANTLE_1M_RH;
        const LocoSlot far_low = left ? LocoSlot::MANTLE_1M_RH : LocoSlot::MANTLE_1M_LH;
        const LocoSlot order[3] = { move->mantle_height > MANTLE_SPLIT ? LocoSlot::MANTLE_2M : near_low, move->mantle_height > MANTLE_SPLIT ? near_low : far_low, move->mantle_height > MANTLE_SPLIT ? far_low : LocoSlot::MANTLE_2M };
        LocoSlot slot = LocoSlot::COUNT;
        for (LocoSlot s : order) {
            if (!_slot_clip(p_set, s)) continue;
            slot = s;
            break;
        }
        _switch(r_char, State::MANTLE, slot, p_set);
        r_char.slot = slot;
    }

    if (move && r_char.state != State::ROLL && r_char.state != State::MANTLE && r_char.state != State::AIR && (move->jumped || (!move->grounded && move->air_time > AIR_DELAY))) {
        LocoSlot takeoff = LocoSlot::COUNT;
        if (move->jumped && speed > JUMP_STILL) {
            const bool run = speed > (move->walk_speed + move->run_speed) * 0.5f;
            const bool left = r_char.foot_down[(int)ClipFootSide::LEFT];
            const LocoSlot near_walk = left ? LocoSlot::JUMP_WALK_LF : LocoSlot::JUMP_WALK_RF;
            const LocoSlot far_walk = left ? LocoSlot::JUMP_WALK_RF : LocoSlot::JUMP_WALK_LF;
            const LocoSlot near_run = left ? LocoSlot::JUMP_RUN_LF : LocoSlot::JUMP_RUN_RF;
            const LocoSlot far_run = left ? LocoSlot::JUMP_RUN_RF : LocoSlot::JUMP_RUN_LF;
            const LocoSlot order[4] = { run ? near_run : near_walk, run ? far_run : far_walk, run ? near_walk : near_run, run ? far_walk : far_run };
            for (LocoSlot s : order) {
                if (!_slot_clip(p_set, s)) continue;
                takeoff = s;
                break;
            }
        }
        r_char.takeoff = takeoff;
        r_char.air_clock = 0.0f;
        _switch(r_char, State::AIR, takeoff == LocoSlot::COUNT ? LocoSlot::JUMP_LOOP : takeoff, p_set);
    }
    if (move) move->jumped = false;

    switch (r_char.state) {
        case State::ROLL:
        case State::MANTLE:
            break;
        case State::AIR: {
            if (!move || !move->grounded) break;
            const float impact = move->land_speed;
            if (impact > LAND_ROLL_SPEED && wants && _slot_clip(p_set, LocoSlot::ROLL)) {
                move->want_roll = true;
                break;
            }
            if (impact > LAND_HEAVY_SPEED && _slot_clip(p_set, LocoSlot::LAND_HEAVY)) {
                move->velocity.x *= LAND_HEAVY_KEEP;
                move->velocity.z *= LAND_HEAVY_KEEP;
                _switch(r_char, State::LAND, LocoSlot::LAND_HEAVY, p_set);
                break;
            }
            if (!wants && speed < LAND_STILL && _slot_clip(p_set, LocoSlot::LAND_LIGHT)) {
                _switch(r_char, State::LAND, LocoSlot::LAND_LIGHT, p_set);
                break;
            }
            r_char.dip = std::clamp(impact / LAND_HEAVY_SPEED, 0.0f, 1.0f);
            r_char.dip_time = 0.0f;
            r_char.cycle = 0.0f;
            if (wants || speed >= LAND_STILL) _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
            else _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            break;
        }
        case State::LAND: {
            const Clip* clip = _slot_clip(p_set, r_char.slot);
            const float x = clip && clip->anim.duration > 0.0f ? r_char.time / clip->anim.duration : 1.0f;
            const float exit = r_char.slot == LocoSlot::LAND_HEAVY ? LAND_HEAVY_EXIT : 0.0f;
            if (wants && x >= exit) {
                r_char.cycle = 0.0f;
                _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
            }
            break;
        }
        case State::TRANSITION:
        case State::IDLE: {
            if (wants || speed > START_SPEED) {
                r_char.cycle = 0.0f;
                _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
                break;
            }
            if (!move) break;
            const float diff = _wrap_angle(move->target_facing - move->facing);
            const CameraComponent* cam = r_world.try_get<CameraComponent>(p_entity);
            if (cam && cam->type == CameraType::FIRST_PERSON) {
                if (std::abs(diff) > glm::radians(p_set.turn_threshold)) {
                    const LocoSlot slot = pick(diff > 0.0f ? LocoSlot::ROTATE_L : LocoSlot::ROTATE_R);
                    if (_slot_clip(p_set, slot)) {
                        _switch(r_char, State::ROTATE, slot, p_set);
                        break;
                    }
                    r_char.aligning = true;
                }
                float facing = move->facing;
                if (r_char.aligning) {
                    const float max_turn = glm::radians(move->turn_rate) * p_dt;
                    facing = _wrap_angle(facing + std::clamp(diff, -max_turn, max_turn));
                    if (std::abs(diff) <= max_turn) r_char.aligning = false;
                }
                const float limit = std::min(glm::radians(p_set.turn_threshold), r_char.yaw_max);
                const float left = _wrap_angle(move->target_facing - facing);
                if (std::abs(left) > limit) facing = _wrap_angle(move->target_facing - (left > 0.0f ? limit : -limit));
                move->facing = facing;
                if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) t->rotation = angleAxis(move->facing, LOCO_UP);
                break;
            }
            if (std::abs(diff) <= glm::radians(p_set.turn_threshold)) {
                r_char.turn_wait = 0.0f;
                break;
            }
            r_char.turn_wait += p_dt;
            if (std::abs(diff) <= glm::radians(TURN_BIG) && r_char.turn_wait < TURN_DECIDE) break;
            r_char.turn_wait = 0.0f;
            bool big = std::abs(diff) > glm::radians(TURN_BIG);
            LocoSlot slot = pick(diff > 0.0f ? (big ? LocoSlot::TURN_L180 : LocoSlot::TURN_L90) : (big ? LocoSlot::TURN_R180 : LocoSlot::TURN_R90));
            if (!_slot_clip(p_set, slot)) {
                big = false;
                slot = pick(diff > 0.0f ? LocoSlot::TURN_L90 : LocoSlot::TURN_R90);
            }
            if (!_slot_clip(p_set, slot)) {
                const float max_turn = glm::radians(move->turn_rate) * p_dt;
                move->facing = _wrap_angle(move->facing + std::clamp(diff, -max_turn, max_turn));
                if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) t->rotation = angleAxis(move->facing, LOCO_UP);
                break;
            }
            _switch(r_char, State::TURN, slot, p_set);
            r_char.turn_total = diff;
            r_char.turn_start = move->facing;
            r_char.turn_clip = big ? glm::pi<float>() : glm::half_pi<float>();
            if (diff < 0.0f) r_char.turn_clip = -r_char.turn_clip;
            if (const Clip* turn = _slot_clip(p_set, slot); turn && std::abs(turn->anim.meta.turn) > ROOT_MOTION_TURN && !turn->anim.meta.trajectory.empty()) {
                r_char.turn_clip = turn->anim.meta.trajectory.back().w - turn->anim.meta.trajectory.front().w;
            }
            break;
        }
        case State::MOVE: {
            if (!wants && speed < IDLE_SPEED) _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            break;
        }
        case State::ROTATE: {
            const CameraComponent* cam = r_world.try_get<CameraComponent>(p_entity);
            if (wants) {
                r_char.cycle = 0.0f;
                _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
            } else if (!move || !cam || cam->type != CameraType::FIRST_PERSON) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            } else {
                const float diff = _wrap_angle(move->target_facing - move->facing);
                const LocoSlot slot = pick(diff > 0.0f ? LocoSlot::ROTATE_L : LocoSlot::ROTATE_R);
                if (std::abs(diff) < glm::radians(ROTATE_DONE)) _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                else if (slot != r_char.slot && _slot_clip(p_set, slot)) _switch(r_char, State::ROTATE, slot, p_set);
            }
            break;
        }
        case State::TURN: {
            if (wants) {
                r_char.cycle = 0.0f;
                _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
                break;
            }
            const LocoSlot stand = loco_stand_slot(r_char.slot);
            if (!move || (stand != LocoSlot::TURN_L90 && stand != LocoSlot::TURN_R90)) break;
            const Clip* current = _slot_clip(p_set, r_char.slot);
            if (!current || current->anim.duration <= 0.0f || r_char.time > current->anim.duration * TURN_UPGRADE) break;
            const float live = _wrap_angle(move->target_facing - r_char.turn_start);
            if (std::abs(live) <= glm::radians(TURN_BIG) || live * r_char.turn_total <= 0.0f) break;
            const LocoSlot bigger = pick(live > 0.0f ? LocoSlot::TURN_L180 : LocoSlot::TURN_R180);
            const Clip* clip = _slot_clip(p_set, bigger);
            if (!clip || std::abs(clip->anim.meta.turn) <= ROOT_MOTION_TURN || clip->anim.meta.trajectory.empty()) break;
            const float progress = r_char.time / current->anim.duration;
            r_char.slot = bigger;
            r_char.time = progress * clip->anim.duration;
            r_char.turn_total = live;
            r_char.turn_clip = clip->anim.meta.trajectory.back().w - clip->anim.meta.trajectory.front().w;
            r_char.turn_start = _wrap_angle(move->facing - live * ((clip->anim.trajectory_at(r_char.time).w - clip->anim.trajectory_at(0.0f).w) / r_char.turn_clip));
            r_char.blend_pending = !r_char.last.empty();
            r_char.blend_time = std::max(p_set.blend_time, 0.0f);
            break;
        }
    }

    r_char.sampled = r_char.state;
    switch (r_char.state) {
        case State::IDLE: {
            const LocoSlot slot = pick(LocoSlot::IDLE);
            const Clip* idle = _slot_clip(p_set, slot);
            if (idle && idle->anim.duration > 0.0f) r_char.time = std::fmod(r_char.time + p_dt, idle->anim.duration);
            _stance_sample(r_char, p_set, p_skeleton, p_skeleton_guid, slot, r_char.time, false, r_char.out);
            break;
        }
        case State::ROLL: {
            const Clip* clip = _slot_clip(p_set, LocoSlot::ROLL);
            if (!clip || !move) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.out = p_skeleton.rest;
                break;
            }
            const float t0 = r_char.time;
            r_char.time = std::min(r_char.time + p_dt * r_char.roll_rate, clip->anim.duration);
            const vec3 before = move->root_motion;
            _push_root(move, *clip, _travel(p_set, LocoSlot::ROLL, p_skeleton, p_skeleton_guid, r_char.time) - _travel(p_set, LocoSlot::ROLL, p_skeleton, p_skeleton_guid, t0), r_char.time);
            if (p_dt > 0.0f) {
                const vec3 ahead(std::sin(move->facing), 0.0f, std::cos(move->facing));
                const float base = std::max(wants ? std::min(r_char.roll_entry, move->gait_speed(move->want_gait)) : r_char.roll_entry, ROLL_SPEED_MIN);
                const float progress = r_char.time / clip->anim.duration;
                const float carry = (wants ? base : base * (1.0f - progress) * (1.0f - progress)) * p_dt;
                const float along = dot(move->root_motion - before, ahead);
                if (along < carry) move->root_motion += ahead * (carry - along);
            }
            if (p_dt > 0.0f) r_char.roll_velocity = (move->root_motion - before) / p_dt;
            _sample(p_set, LocoSlot::ROLL, p_skeleton, p_skeleton_guid, r_char.time, r_char.out);
            const float x = r_char.time / clip->anim.duration;
            move->roll_camera = std::sin(x * glm::pi<float>());
            const bool early = wants && x >= ROLL_EXIT;
            if (early || r_char.time >= clip->anim.duration) {
                move->rolling = false;
                move->roll_camera = 0.0f;
                vec3 exit(r_char.roll_velocity.x, 0.0f, r_char.roll_velocity.z);
                if (wants) {
                    const vec3 ahead(std::sin(move->facing), 0.0f, std::cos(move->facing));
                    const float carry = std::max(length(exit), std::min(r_char.roll_entry, move->gait_speed(move->want_gait)));
                    exit = ahead * carry;
                }
                move->velocity.x = exit.x;
                move->velocity.z = exit.z;
                r_char.cycle = 0.0f;
                if (wants) _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
                else _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.blend_time = std::max(p_set.blend_time, ROLL_BLEND_OUT);
            }
            break;
        }
        case State::TRANSITION: {
            const Clip* clip = _slot_clip(p_set, r_char.slot);
            if (!clip) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.out = p_skeleton.rest;
                break;
            }
            const float t0 = r_char.time;
            r_char.time = std::min(r_char.time + p_dt, clip->anim.duration);
            _push_root(move, *clip, _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, r_char.time) - _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, t0), r_char.time);
            _sample(p_set, r_char.slot, p_skeleton, p_skeleton_guid, r_char.time, r_char.out);
            if (r_char.time >= clip->anim.duration) _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            break;
        }
        case State::MANTLE: {
            if (!move) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                break;
            }
            const Clip* clip = r_char.slot != LocoSlot::COUNT ? _slot_clip(p_set, r_char.slot) : nullptr;
            const float duration = clip && clip->anim.duration > 0.0f ? clip->anim.duration : MANTLE_FALLBACK_TIME;
            r_char.time = std::min(r_char.time + p_dt, duration);
            const float x = r_char.time / duration;
            const vec3 forward(std::sin(move->mantle_facing), 0.0f, std::cos(move->mantle_facing));
            const vec3 delta = move->mantle_target - move->mantle_start;
            const float along = dot(delta, forward);
            vec3 offset(0.0f, delta.y, along);
            const std::vector<vec3>* path = clip ? _mantle_path(r_char, p_set, r_char.slot, p_skeleton, p_skeleton_guid) : nullptr;
            vec3 now(0.0f);
            if (path && path->size() >= 2) {
                const float frame = x * (float)(path->size() - 1);
                const uint32_t f0 = std::min((uint32_t)frame, (uint32_t)path->size() - 2);
                now = mix((*path)[f0], (*path)[f0 + 1], frame - (float)f0);
                const vec3 end = path->back();
                const float sy = std::abs(end.y) > 1e-3f ? std::clamp(delta.y / end.y, MANTLE_SCALE_MIN, MANTLE_SCALE_MAX) : 1.0f;
                const float sf = std::abs(end.z) > 1e-3f ? std::clamp(along / end.z, MANTLE_SCALE_MIN, MANTLE_SCALE_MAX) : 1.0f;
                offset = vec3(0.0f, now.y * sy, now.z * sf);
            } else {
                const float rise = std::clamp(x / 0.6f, 0.0f, 1.0f);
                const float reach = std::clamp((x - 0.3f) / 0.7f, 0.0f, 1.0f);
                offset = vec3(0.0f, delta.y * rise * rise * (3.0f - 2.0f * rise), along * reach * reach * (3.0f - 2.0f * reach));
            }
            const vec3 world = move->mantle_start + angleAxis(move->mantle_facing, LOCO_UP) * offset;
            const bool on_top = offset.y >= delta.y - MANTLE_TOP_TOLERANCE;
            const bool early = wants && x >= MANTLE_EXIT && on_top;
            const vec3 placed = x >= 1.0f ? move->mantle_target : (early ? vec3(world.x, std::max(world.y, move->mantle_target.y), world.z) : world);
            const vec3 step = r_char.time > p_dt ? placed - r_char.mantle_last : vec3(0.0f);
            r_char.mantle_last = placed;
            move->facing = move->mantle_facing;
            if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) {
                t->position = placed;
                t->rotation = angleAxis(move->facing, LOCO_UP);
            }
            if (clip) {
                const Binding* b = _binding(p_set.clip(r_char.slot), *clip, p_skeleton, p_skeleton_guid);
                r_char.out = p_skeleton.rest;
                clip_sample(BoundClip{ &clip->anim, &b->map, &b->retarget }, r_char.time, r_char.out.data());
                if (r_char.top != SKELETON_NO_JOINT && path) r_char.out[r_char.top].translation -= now;
            } else if (r_char.last.size() == r_char.out.size()) {
                r_char.out = r_char.last;
            }
            move->roll_camera = std::sin(x * glm::pi<float>());
            if (x >= 1.0f || early) {
                move->mantling = false;
                move->mantle_sync = true;
                move->roll_camera = 0.0f;
                move->velocity = early && p_dt > 0.0f ? vec3(step.x, 0.0f, step.z) / p_dt : vec3(0.0f);
                r_char.cycle = 0.0f;
                if (wants) _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
                else _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.blend_time = std::max(p_set.blend_time, MANTLE_BLEND_OUT);
            }
            break;
        }
        case State::AIR: {
            r_char.time += p_dt;
            r_char.air_clock += p_dt;
            const float vy = move ? move->velocity.y : 0.0f;
            auto loop = [&](LocoSlot p_slot, std::vector<JointTransform>& r_pose) {
                const Clip* clip = _slot_clip(p_set, p_slot);
                return clip && clip->anim.duration > 0.0f && _sample(p_set, p_slot, p_skeleton, p_skeleton_guid, std::fmod(r_char.air_clock, clip->anim.duration), r_pose);
            };
            const bool rise = loop(LocoSlot::JUMP_LOOP, r_char.out);
            bool have = rise;
            const float fall = std::clamp((AIR_APEX - vy) / (2.0f * AIR_APEX), 0.0f, 1.0f);
            if (loop(LocoSlot::FALL_LOOP, r_char.stance_pose)) {
                if (have) _mix_pose(r_char.out, r_char.stance_pose, fall * fall * (3.0f - 2.0f * fall));
                else r_char.out = r_char.stance_pose;
                have = true;
            }
            const float fast = std::clamp((-vy - FALL_FAST_FROM) / FALL_FAST_RANGE, 0.0f, 1.0f);
            if (fast > 1e-3f && loop(LocoSlot::FALL_FAST, r_char.stance_pose)) {
                if (have) _mix_pose(r_char.out, r_char.stance_pose, fast);
                else r_char.out = r_char.stance_pose;
                have = true;
            }
            const Clip* takeoff = r_char.takeoff != LocoSlot::COUNT ? _slot_clip(p_set, r_char.takeoff) : nullptr;
            if (takeoff && r_char.time < takeoff->anim.duration && _sample(p_set, r_char.takeoff, p_skeleton, p_skeleton_guid, r_char.time, r_char.stance_pose)) {
                const float left = takeoff->anim.duration - r_char.time;
                const float w = have ? std::clamp(left / TAKEOFF_FADE, 0.0f, 1.0f) : 1.0f;
                if (have) _mix_pose(r_char.out, r_char.stance_pose, w);
                else r_char.out = r_char.stance_pose;
                have = true;
            }
            if (!have) _cycle(r_char, p_set, p_skeleton, p_skeleton_guid, move, p_dt, r_char.stance >= 0.5f, true, r_char.out);
            break;
        }
        case State::LAND: {
            const Clip* clip = _slot_clip(p_set, r_char.slot);
            if (!clip) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.out = p_skeleton.rest;
                break;
            }
            r_char.time = std::min(r_char.time + p_dt, clip->anim.duration);
            _sample(p_set, r_char.slot, p_skeleton, p_skeleton_guid, r_char.time, r_char.out);
            if (r_char.time >= clip->anim.duration) {
                r_char.cycle = 0.0f;
                if (wants) _switch(r_char, State::MOVE, LocoSlot::WALK_F, p_set);
                else _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            }
            break;
        }
        case State::MOVE: {
            const bool dominant = r_char.stance >= 0.5f;
            _cycle(r_char, p_set, p_skeleton, p_skeleton_guid, move, p_dt, dominant, true, r_char.out);
            const float other = dominant ? 1.0f - r_char.stance : r_char.stance;
            if (other > 1e-3f && _cycle(r_char, p_set, p_skeleton, p_skeleton_guid, move, p_dt, !dominant, false, r_char.stance_pose)) _mix_pose(r_char.out, r_char.stance_pose, other);
            _effort(r_char, p_set, p_skeleton, p_skeleton_guid);
            break;
        }
        case State::ROTATE: {
            const Clip* clip = _slot_clip(p_set, r_char.slot);
            if (!clip || !move || clip->anim.duration <= 0.0f) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.out = p_skeleton.rest;
                break;
            }
            const float threshold = std::min(glm::radians(p_set.turn_threshold), r_char.yaw_max);
            const float diff = _wrap_angle(move->target_facing - move->facing);
            const float rate = std::clamp(std::abs(diff) / std::max(threshold, 1e-3f), 1.0f, MAX_RATE);
            const float duration = clip->anim.duration;
            const float prev = r_char.time;
            const float next = prev + p_dt * rate;
            const float wrapped = std::fmod(next, duration);
            float delta = 0.0f;
            if (std::abs(clip->anim.meta.turn) > ROOT_MOTION_TURN && !clip->anim.meta.trajectory.empty()) {
                auto yaw = [&](float p_t) { return clip->anim.trajectory_at(p_t).w; };
                delta = next >= duration ? (yaw(duration) - yaw(prev)) + (yaw(wrapped) - yaw(0.0f)) : yaw(next) - yaw(prev);
            } else {
                delta = glm::radians(90.0f) / duration * rate * p_dt * (diff > 0.0f ? 1.0f : -1.0f);
            }
            if (delta * diff > 0.0f && std::abs(delta) > std::abs(diff)) delta = diff;
            float facing = _wrap_angle(move->facing + delta);
            const float left = _wrap_angle(move->target_facing - facing);
            if (std::abs(left) > threshold) facing = _wrap_angle(move->target_facing - (left > 0.0f ? threshold : -threshold));
            move->facing = facing;
            if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) t->rotation = angleAxis(move->facing, LOCO_UP);
            const vec3 moved = next >= duration
                ? (_travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, duration) - _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, prev)) + _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, wrapped)
                : _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, next) - _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, prev);
            r_char.time = wrapped;
            _push_root(move, *clip, moved, r_char.time);
            _stance_sample(r_char, p_set, p_skeleton, p_skeleton_guid, r_char.slot, r_char.time / duration, true, r_char.out);
            break;
        }
        case State::TURN: {
            const Clip* clip = _slot_clip(p_set, r_char.slot);
            if (!clip || !move) {
                _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
                r_char.out = p_skeleton.rest;
                break;
            }
            const float t0 = r_char.time;
            r_char.time = std::min(r_char.time + p_dt, clip->anim.duration);
            const bool authored_turn = std::abs(clip->anim.meta.turn) > ROOT_MOTION_TURN && std::abs(r_char.turn_clip) > 1e-3f;
            float progress = clip->anim.duration > 0.0f ? r_char.time / clip->anim.duration : 1.0f;
            if (authored_turn) progress = (clip->anim.trajectory_at(r_char.time).w - clip->anim.trajectory_at(0.0f).w) / r_char.turn_clip;
            move->facing = _wrap_angle(r_char.turn_start + r_char.turn_total * progress);
            if (TransformComponent* t = r_world.try_get<TransformComponent>(p_entity)) t->rotation = angleAxis(move->facing, LOCO_UP);
            _push_root(move, *clip, _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, r_char.time) - _travel(p_set, r_char.slot, p_skeleton, p_skeleton_guid, t0), r_char.time);
            _stance_sample(r_char, p_set, p_skeleton, p_skeleton_guid, r_char.slot, clip->anim.duration > 0.0f ? r_char.time / clip->anim.duration : 1.0f, true, r_char.out);
            if (r_char.time >= clip->anim.duration) _switch(r_char, State::IDLE, LocoSlot::IDLE, p_set);
            break;
        }
    }

    if (r_char.state != State::TRANSITION && r_char.stance_slot != LocoSlot::COUNT && r_char.stance_x < 1.0f && r_char.upper.size() == r_char.out.size()) {
        const float x = r_char.stance_x;
        const float w = std::sin(x * glm::pi<float>()) * STANCE_UPPER;
        if (w > 1e-3f && _sample(p_set, r_char.stance_slot, p_skeleton, p_skeleton_guid, r_char.stance_time, r_char.stance_pose)) {
            for (size_t j = 0; j < r_char.out.size(); ++j) {
                if (r_char.upper[j]) r_char.out[j].rotation = normalize(slerp(r_char.out[j].rotation, r_char.stance_pose[j].rotation, w));
            }
        }
    }
    if (r_char.yaw_blend != 0.0f && r_char.top != SKELETON_NO_JOINT && r_char.top < (int32_t)r_char.out.size()) {
        r_char.yaw_elapsed += p_dt;
        const float x = std::clamp(r_char.yaw_elapsed / std::max(r_char.yaw_time, 1e-3f), 0.0f, 1.0f);
        const float k = 1.0f - x * x * (3.0f - 2.0f * x);
        const quat turn = angleAxis(r_char.yaw_blend * k, LOCO_UP);
        r_char.out[r_char.top].rotation = normalize(turn * r_char.out[r_char.top].rotation);
        r_char.out[r_char.top].translation = turn * r_char.out[r_char.top].translation;
        if (x >= 1.0f) r_char.yaw_blend = 0.0f;
    }
    if (r_char.dip > 0.0f) {
        r_char.dip_time += p_dt;
        const float x = r_char.dip_time / DIP_TIME;
        if (x >= 1.0f) r_char.dip = 0.0f;
        else _dip(r_char, p_set, p_skeleton, std::sin(x * glm::pi<float>()) * r_char.dip);
    }
    if (move && r_char.state != State::ROLL) move->rolling = false;
    if (move && r_char.state != State::MANTLE) move->mantling = false;
    if (move && r_char.state != State::ROLL && r_char.state != State::MANTLE) move->roll_camera = 0.0f;
    if (move) move->foot_planted = r_char.state != State::MOVE || r_char.foot_grip;
    _lean(r_char, p_set, p_skeleton, move, p_dt);
    _blend(r_char, p_dt);
    r_char.last = r_char.out;
}

void LocomotionSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !renderer) return;
    Scene& world = *r_session.world;
    for (auto& [k, c] : characters) c.seen = false;

    std::vector<Entity> entities;
    world.view<MeshComponent, LocomotionComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const MeshComponent&, const LocomotionComponent&) { entities.push_back(p_entity); });

    for (Entity e : entities) {
        const MeshComponent& mesh = *world.try_get<MeshComponent>(e);
        const LocomotionComponent& loco = *world.try_get<LocomotionComponent>(e);
        const LMeshRig* rig = renderer->geometry.rig(renderer->mesh_slots.mesh_index(mesh.mesh));
        if (!rig) continue;
        const SkeletonCache::Entry* entry = renderer->skeletons.get(rig->skeleton);
        if (!entry || entry->skeleton.joint_count() == 0) continue;
        const LSkeleton& skeleton = entry->skeleton;
        const LLocomotionSet* set = _set(loco.set);
        if (!set) continue;

        Character& c = characters[_entity_key(e)];
        c.seen = true;
        if (c.skeleton != rig->skeleton || c.set != loco.set || c.out.size() != skeleton.joint_count()) {
            c = Character{};
            c.seen = true;
            c.skeleton = rig->skeleton;
            c.set = loco.set;
            c.out = skeleton.rest;
            std::vector<int32_t> spine;
            for (uint32_t i = 0; i < skeleton.joint_count(); ++i) {
                if (_lower(skeleton.names[i]).find("spine") != std::string::npos) spine.push_back((int32_t)i);
            }
            c.lean_chain = spine;
            for (uint32_t i = 0; i < skeleton.joint_count(); ++i) {
                const std::string lower = _lower(skeleton.names[i]);
                if (lower.find("arm") == std::string::npos || lower.find("twist") != std::string::npos || lower.find("armature") != std::string::npos) continue;
                c.arms.push_back((int32_t)i);
            }
            if (!spine.empty()) {
                c.upper.assign(skeleton.joint_count(), 0);
                for (uint32_t i = 0; i < skeleton.joint_count(); ++i) {
                    for (int32_t j = (int32_t)i; j >= 0; j = skeleton.parents[j]) {
                        if (j != spine.front()) continue;
                        c.upper[i] = 1;
                        break;
                    }
                }
            }
        }

        const LookAtComponent* look = world.try_get<LookAtComponent>(e);
        c.yaw_max = glm::radians(look ? look->yaw_max : LookAtComponent::DEFAULT_YAW_MAX);
        _evaluate(world, e, c, *set, skeleton, rig->skeleton, p_dt);

        PoseComponent* pose = world.try_get<PoseComponent>(e);
        if (!pose) {
            world.add<PoseComponent>(e);
            pose = world.try_get<PoseComponent>(e);
        }
        if (!pose) continue;
        pose->skeleton = rig->skeleton;
        pose->local = c.out;
        pose->joints.resize(skeleton.joint_count());
        skeleton.globals(c.out.data(), pose->joints.data());
        const CameraComponent* cam = world.try_get<CameraComponent>(e);
        pose->hidden_joint = -1;
        if (cam && cam->type == CameraType::FIRST_PERSON && cam->hide_head && cam->viewing && cam->third_person < HEAD_SHOW) {
            const int32_t head = skeleton_find_head(skeleton, cam->head_bone);
            if (head != SKELETON_NO_JOINT) pose->hidden_joint = head;
        }
    }

    for (auto it = characters.begin(); it != characters.end();) {
        if (it->second.seen) ++it;
        else it = characters.erase(it);
    }
}

}
