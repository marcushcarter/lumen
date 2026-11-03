#include <core/animation/clip_player.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void ClipPlayer::advance(float p_dt, float p_duration)
{
    if (!playing || p_duration <= 0.0f) return;
    time += p_dt * speed;
    if (loop) {
        time = std::fmod(time, p_duration);
        if (time < 0.0f) time += p_duration;
    } else if (time >= p_duration || time <= 0.0f) {
        time = std::clamp(time, 0.0f, p_duration);
        playing = false;
    }
}

void clip_track_map(const LSkeleton& p_skeleton, const LAnimation& p_clip, std::vector<int32_t>& r_map)
{
    r_map.resize(p_clip.track_count());
    for (uint32_t t = 0; t < p_clip.track_count(); ++t) r_map[t] = p_skeleton.find(p_clip.tracks[t]);
}

void clip_sample(const LAnimation& p_clip, const std::vector<int32_t>& p_map, float p_time, JointTransform* r_local)
{
    for (uint32_t t = 0; t < (uint32_t)p_map.size(); ++t) {
        if (p_map[t] != SKELETON_NO_JOINT) r_local[p_map[t]] = p_clip.sample(t, p_time);
    }
}

}
