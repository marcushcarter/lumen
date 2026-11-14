#include <core/animation/anim_eval.h>
#include <core/animation/blend_space.h>
#include <core/animation/clip_player.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void AnimEvaluator::blend_weights(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value, std::vector<float>& r_weights)
{
    const size_t count = p_space.samples.size();
    _usable.assign(count, 0);
    for (size_t i = 0; i < count; i++) {
        const BoundClip c = r_clips.clip(p_space.samples[i].clip);
        _usable[i] = c.valid() && c.clip->duration > 0.0f ? 1 : 0;
    }
    r_weights.assign(count, 0.0f);
    if (count) blend_space_weights(p_space, (const bool*)_usable.data(), p_value, r_weights.data());
}

AnimEvaluator::BlendInfo AnimEvaluator::blend_info(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value)
{
    blend_weights(r_clips, p_space, p_value, _weights);
    BlendInfo info;
    float total = 0.0f;
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (_weights[i] <= BLEND_MIN_WEIGHT) continue;
        const BoundClip c = r_clips.clip(p_space.samples[i].clip);
        if (!c.valid()) continue;
        total += _weights[i];
        info.duration += _weights[i] * c.clip->duration;
        info.speed += _weights[i] * p_space.samples[i].speed;
    }
    if (total <= 0.0f) return info;
    info.duration /= total;
    info.speed /= total;
    info.valid = info.duration > 0.0f;
    return info;
}

bool AnimEvaluator::blend_sample(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_phase, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    if (joints == 0) return false;
    blend_weights(r_clips, p_space, p_value, _weights);
    _inputs.clear();
    _sample_poses.resize(p_space.samples.size());
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (_weights[i] <= BLEND_MIN_WEIGHT) continue;
        const BoundClip c = r_clips.clip(p_space.samples[i].clip);
        if (!c.valid()) continue;
        _sample_poses[i] = p_skeleton.rest;
        clip_sample(*c.clip, *c.map, p_phase * c.clip->duration, _sample_poses[i].data());
        _inputs.push_back({ _sample_poses[i].data(), _weights[i] });
    }
    if (_inputs.empty()) return false;
    pose_blend(_inputs.data(), (uint32_t)_inputs.size(), joints, r_pose);
    return true;
}

bool AnimEvaluator::blend_pose(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_rate, float p_dt, float& r_phase, JointTransform* r_pose)
{
    const BlendInfo info = blend_info(r_clips, p_space, p_value);
    if (!info.valid) return false;
    r_phase += p_dt * p_rate / info.duration;
    r_phase -= std::floor(r_phase);
    return blend_sample(r_clips, p_skeleton, p_space, p_value, r_phase, r_pose);
}

void AnimEvaluator::overlay_apply(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LOverlay& p_overlay, const float* p_mask, float p_time, float p_weight, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    const BoundClip source = r_clips.clip(p_overlay.source);
    if (joints == 0 || !source.valid() || p_weight <= 0.0f) return;

    _layer = p_skeleton.rest;
    const float duration = source.clip->duration;
    clip_sample(*source.clip, *source.map, duration > 0.0f ? std::fmod(p_time, duration) : 0.0f, _layer.data());

    if (p_overlay.mode == OverlayMode::OVERRIDE) {
        pose_layer(r_pose, _layer.data(), p_weight, p_mask, joints);
        return;
    }
    _reference = p_skeleton.rest;
    if (const BoundClip reference = r_clips.clip(p_overlay.reference); reference.valid()) clip_sample(*reference.clip, *reference.map, 0.0f, _reference.data());
    pose_additive_delta(_layer.data(), _reference.data(), joints, _layer.data());
    pose_add(r_pose, _layer.data(), p_weight, p_mask, joints);
}

}
