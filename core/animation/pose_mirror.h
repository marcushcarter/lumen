#pragma once
#include <core/assets/lskeleton.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct PoseMirror
{
    Guid skeleton;
    uint32_t joints = 0;
    std::vector<int32_t> pair;
    std::vector<mat4> correction;
    mat4 reflect = mat4(1.0f);
    std::vector<mat4> _global;
    std::vector<mat4> _mirrored;

    void bind(const LSkeleton& p_skeleton);
    void apply(const LSkeleton& p_skeleton, JointTransform* r_local);
};

}
