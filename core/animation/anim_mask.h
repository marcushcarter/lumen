#pragma once
#include <core/assets/lskeleton.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct AnimMask
{
    std::vector<std::string> branches;
    std::vector<std::string> exclude;
    uint32_t depth = 0;

    bool operator==(const AnimMask&) const = default;
};

void anim_mask_weights(const AnimMask& p_mask, const LSkeleton& p_skeleton, std::vector<float>& r_weights);

}
