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

void clip_track_map(const LSkeleton& p_skeleton, const LAnimation& p_clip, std::vector<int32_t>& r_map);
void clip_sample(const LAnimation& p_clip, const std::vector<int32_t>& p_map, float p_time, JointTransform* r_local);

}
