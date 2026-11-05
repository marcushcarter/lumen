#include <core/animation/anim_eval.h>
#include <core/animation/blend_space.h>
#include <core/animation/clip_player.h>
#include <cmath>

namespace lumen {

void AnimEvaluator::blend_weights(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value, float p_direction, std::vector<float>& r_weights)
{
    _positions.clear();
    _directions.clear();
    _usable.clear();
    for (uint32_t i = 0; i < (uint32_t)p_space.samples.size(); i++) {
        if (!r_clips.clip(p_space.samples[i].clip).valid()) continue;
        _positions.push_back(p_space.samples[i].position);
        _directions.push_back(p_space.samples[i].direction);
        _usable.push_back(i);
    }
    _usable_weights.assign(_positions.size(), 0.0f);
    if (p_space.kind == BlendSpaceKind::TWO_D) blend_space_polar_weights(_positions.data(), _directions.data(), (uint32_t)_positions.size(), p_value, p_direction, _usable_weights.data());
    else blend_space_1d_weights(_positions.data(), (uint32_t)_positions.size(), p_value, _usable_weights.data());
    r_weights.assign(p_space.samples.size(), 0.0f);
    for (size_t k = 0; k < _usable.size(); k++) r_weights[_usable[k]] = _usable_weights[k];
}

bool AnimEvaluator::blend_pose(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_direction, float p_advance, float& r_phase, JointTransform* r_pose)
{
    const uint32_t joints = p_skeleton.joint_count();
    if (joints == 0) return false;

    blend_weights(r_clips, p_space, p_value, p_direction, _weights);
    _durations.assign(p_space.samples.size(), 0.0f);
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (const BoundClip c = r_clips.clip(p_space.samples[i].clip); c.valid()) _durations[i] = c.clip->duration;
    }
    const float cycle = blend_cycle_duration(_durations.data(), _weights.data(), (uint32_t)_durations.size());
    if (cycle <= 0.0f) return false;
    r_phase += p_advance / cycle;
    r_phase -= std::floor(r_phase);

    _inputs.clear();
    _sample_poses.resize(p_space.samples.size());
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (_weights[i] <= 0.0f) continue;
        const BoundClip c = r_clips.clip(p_space.samples[i].clip);
        if (!c.valid()) continue;
        _sample_poses[i] = p_skeleton.rest;
        clip_sample(*c.clip, *c.map, r_phase * c.clip->duration, _sample_poses[i].data());
        _inputs.push_back({ _sample_poses[i].data(), _weights[i] });
    }
    if (_inputs.empty()) return false;
    pose_blend(_inputs.data(), (uint32_t)_inputs.size(), joints, r_pose);
    return true;
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
