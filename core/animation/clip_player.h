#pragma once
#include <core/assets/lanim.h>
#include <core/assets/lskeleton.h>
#include <vector>

namespace lumen {

struct ClipPlayer
{
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = true;
    bool loop = true;

    void advance(float p_dt, float p_duration);
};

enum class RetargetMode : uint8_t
{
    ANIMATION,
    SKELETON,
    SCALED,
};

struct ClipRetarget
{
    std::vector<RetargetMode> modes;
    std::vector<vec3> translations;
    float scale = 1.0f;

    bool active() const { return !modes.empty(); }
};

struct BoundClip
{
    const LAnimation* clip = nullptr;
    const std::vector<int32_t>* map = nullptr;
    const ClipRetarget* retarget = nullptr;

    bool valid() const { return clip && map; }
};

void clip_track_map(const LSkeleton& p_skeleton, const LAnimation& p_clip, std::vector<int32_t>& r_map);
void clip_retarget(const LSkeleton& p_source, const LSkeleton& p_target, const std::vector<int32_t>& p_map, ClipRetarget& r_retarget);
void clip_sample(const BoundClip& p_clip, float p_time, JointTransform* r_local);

}
