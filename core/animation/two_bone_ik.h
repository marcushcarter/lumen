#pragma once
#include <core/assets/lskeleton.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

struct TwoBoneChain
{
    int32_t upper = SKELETON_NO_JOINT;
    int32_t mid = SKELETON_NO_JOINT;
    int32_t end = SKELETON_NO_JOINT;

    bool valid() const { return upper >= 0 && mid >= 0 && end >= 0; }
};

TwoBoneChain two_bone_chain(const LSkeleton& p_skeleton, int32_t p_end);
bool two_bone_ik(const LSkeleton& p_skeleton, std::vector<JointTransform>& r_local, std::vector<mat4>& r_globals, const TwoBoneChain& p_chain, const vec3& p_target, const vec3& p_pole, bool p_keep_end_rotation);

}
