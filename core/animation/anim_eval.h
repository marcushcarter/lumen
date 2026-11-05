#pragma once
#include <core/animation/pose_blend.h>
#include <core/assets/lanim.h>
#include <core/assets/lblendspace.h>
#include <core/assets/loverlay.h>
#include <core/assets/lskeleton.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct BoundClip
{
    const LAnimation* clip = nullptr;
    const std::vector<int32_t>* map = nullptr;

    bool valid() const { return clip && map; }
};

struct ClipProvider
{
    virtual ~ClipProvider() = default;
    virtual BoundClip clip(Guid p_guid) = 0;
};

struct AnimEvaluator
{
    std::vector<float> _positions;
    std::vector<float> _directions;
    std::vector<uint32_t> _usable;
    std::vector<float> _usable_weights;
    std::vector<float> _weights;
    std::vector<float> _durations;
    std::vector<PoseInput> _inputs;
    std::vector<std::vector<JointTransform>> _sample_poses;
    std::vector<JointTransform> _layer;
    std::vector<JointTransform> _reference;

    void blend_weights(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value, float p_direction, std::vector<float>& r_weights);
    bool blend_pose(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_direction, float p_advance, float& r_phase, JointTransform* r_pose);
    void overlay_apply(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LOverlay& p_overlay, const float* p_mask, float p_time, float p_weight, JointTransform* r_pose);
};

}
