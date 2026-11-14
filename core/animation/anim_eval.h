#pragma once
#include <core/animation/blend_space.h>
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
    virtual const LBlendSpace* blend_space(Guid p_guid) = 0;
};

struct AnimEvaluator
{
    struct BlendInfo {
        float duration = 0.0f;
        float speed = 0.0f;
        bool valid = false;
    };

    std::vector<PoseInput> _inputs;
    std::vector<std::vector<JointTransform>> _sample_poses;
    std::vector<JointTransform> _layer;
    std::vector<JointTransform> _reference;
    std::vector<float> _weights;
    std::vector<uint8_t> _usable;

    void blend_weights(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value, std::vector<float>& r_weights);
    BlendInfo blend_info(ClipProvider& r_clips, const LBlendSpace& p_space, float p_value);
    bool blend_sample(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_phase, JointTransform* r_pose);
    bool blend_pose(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LBlendSpace& p_space, float p_value, float p_rate, float p_dt, float& r_phase, JointTransform* r_pose);
    void overlay_apply(ClipProvider& r_clips, const LSkeleton& p_skeleton, const LOverlay& p_overlay, const float* p_mask, float p_time, float p_weight, JointTransform* r_pose);
};

}
