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

static int32_t _hips(const LSkeleton& p_skeleton)
{
    const uint32_t count = p_skeleton.joint_count();
    std::vector<uint32_t> size(count, 1u);
    for (int32_t i = (int32_t)count - 1; i >= 0; --i) {
        if (p_skeleton.parents[i] >= 0) size[p_skeleton.parents[i]] += size[i];
    }
    int32_t best = SKELETON_NO_JOINT;
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t parent = p_skeleton.parents[i];
        if (parent < 0 || p_skeleton.parents[parent] >= 0) continue;
        if (best == SKELETON_NO_JOINT || size[i] > size[best]) best = (int32_t)i;
    }
    return best;
}

static float _hips_height(const LSkeleton& p_skeleton)
{
    const int32_t hips = _hips(p_skeleton);
    if (hips == SKELETON_NO_JOINT) return 0.0f;
    std::vector<mat4> global(p_skeleton.joint_count());
    p_skeleton.globals(p_skeleton.rest.data(), global.data());
    return length(vec3(global[hips][3]) - vec3(global[p_skeleton.parents[hips]][3]));
}

void clip_retarget(const LSkeleton& p_source, const LSkeleton& p_target, const std::vector<int32_t>& p_map, ClipRetarget& r_retarget)
{
    r_retarget = {};
    if (p_source.guid == p_target.guid || p_source.joint_count() == 0 || p_target.joint_count() == 0) return;
    const float source_height = _hips_height(p_source);
    const float target_height = _hips_height(p_target);
    r_retarget.scale = source_height > 1e-4f && target_height > 1e-4f ? target_height / source_height : 1.0f;
    r_retarget.modes.resize(p_map.size(), RetargetMode::ANIMATION);
    r_retarget.translations.resize(p_map.size(), vec3(0.0f));
    for (size_t t = 0; t < p_map.size(); ++t) {
        const int32_t joint = p_map[t];
        if (joint == SKELETON_NO_JOINT) continue;
        const int32_t parent = p_target.parents[joint];
        if (parent < 0) continue;
        if (p_target.parents[parent] < 0) {
            r_retarget.modes[t] = RetargetMode::SCALED;
            continue;
        }
        r_retarget.modes[t] = RetargetMode::SKELETON;
        r_retarget.translations[t] = p_target.rest[joint].translation;
    }
}

void clip_sample(const BoundClip& p_clip, float p_time, JointTransform* r_local)
{
    const std::vector<int32_t>& map = *p_clip.map;
    const ClipRetarget* retarget = p_clip.retarget && p_clip.retarget->active() ? p_clip.retarget : nullptr;
    for (uint32_t t = 0; t < (uint32_t)map.size(); ++t) {
        if (map[t] == SKELETON_NO_JOINT) continue;
        JointTransform sample = p_clip.clip->sample(t, p_time);
        if (retarget) {
            switch (retarget->modes[t]) {
                case RetargetMode::ANIMATION: break;
                case RetargetMode::SKELETON: sample.translation = retarget->translations[t]; break;
                case RetargetMode::SCALED: sample.translation *= retarget->scale; break;
            }
        }
        r_local[map[t]] = sample;
    }
}

}
