#include <core/rendering/frame_data.h>

namespace lumen {

void FrameData::reset()
{
    instances_scratch.clear();
    skins_scratch.clear();
    candidates.clear();
    candidate_count = 0;
    skin_jobs.clear();
    transforms_scratch.clear();
    instance_count = 0;
    cluster_ref_capacity = 0;
    player_instance = UINT32_MAX;
}

uint32_t FrameData::joint_alloc(uint32_t p_count)
{
    for (size_t i = 0; i < joint_free.size(); ++i) {
        if (joint_free[i].y != p_count) continue;
        const uint32_t base = joint_free[i].x;
        joint_free[i] = joint_free.back();
        joint_free.pop_back();
        return base;
    }
    if (joint_top + p_count > MAX_SKIN_JOINTS) {
        joint_reset = true;
        return UINT32_MAX;
    }
    const uint32_t base = joint_top;
    joint_top += p_count;
    return base;
}

void FrameData::joint_release(EntityCache& r_cache)
{
    if (r_cache.joint_base != UINT32_MAX) joint_free.push_back(uvec2(r_cache.joint_base, r_cache.joint_size));
    r_cache.joint_base = UINT32_MAX;
    r_cache.joint_size = 0;
    for (uint32_t k = 0; k < JOINT_FRAMES; ++k) r_cache.written_curr[k] = r_cache.written_prev[k] = UINT64_MAX;
}

void FrameData::joint_clear()
{
    for (EntityCache& c : entity_cache) {
        c.joint_base = UINT32_MAX;
        c.joint_size = 0;
        for (uint32_t k = 0; k < JOINT_FRAMES; ++k) c.written_curr[k] = c.written_prev[k] = UINT64_MAX;
    }
    joint_free.clear();
    joint_top = 0;
    joint_reset = false;
}

}
