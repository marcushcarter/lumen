#include <core/animation/anim_mask.h>
#include <algorithm>

namespace lumen {

static bool _listed(const std::vector<std::string>& p_names, const std::string& p_name)
{
    return std::find(p_names.begin(), p_names.end(), p_name) != p_names.end();
}

void anim_mask_weights(const AnimMask& p_mask, const LSkeleton& p_skeleton, std::vector<float>& r_weights)
{
    const uint32_t joints = p_skeleton.joint_count();
    r_weights.assign(joints, 0.0f);
    const bool full_body = p_mask.branches.empty();
    for (uint32_t j = 0; j < joints; j++) {
        float weight = full_body ? 1.0f : 0.0f;
        uint32_t distance = 0;
        for (int32_t a = (int32_t)j; a != SKELETON_NO_JOINT && distance <= joints; a = p_skeleton.parents[a], distance++) {
            const std::string& name = p_skeleton.names[a];
            if (_listed(p_mask.exclude, name)) {
                weight = 0.0f;
                break;
            }
            if (_listed(p_mask.branches, name)) {
                weight = std::min(1.0f, (float)(distance + 1) / (float)(p_mask.depth + 1));
                break;
            }
        }
        r_weights[j] = weight;
    }
}

}
