#include <core/animation/skinning.h>
#include <cctype>
#include <string>

namespace lumen {

void rig_joint_map(const LSkeleton& p_skeleton, const LMeshRig& p_rig, std::vector<int32_t>& r_map)
{
    r_map.resize(p_rig.joints.size());
    for (size_t j = 0; j < p_rig.joints.size(); ++j) r_map[j] = p_skeleton.find(p_rig.joints[j]);
}

void rig_skin(const std::vector<mat4>& p_skeleton_global, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, std::vector<mat4>& r_skin)
{
    r_skin.resize(p_rig.inverse_binds.size());
    for (size_t j = 0; j < r_skin.size(); ++j) {
        const int32_t s = j < p_map.size() ? p_map[j] : SKELETON_NO_JOINT;
        r_skin[j] = s == SKELETON_NO_JOINT || s >= (int32_t)p_skeleton_global.size() ? mat4(1.0f) : p_skeleton_global[s] * p_rig.inverse_binds[j];
    }
}

int32_t skeleton_find_head(const LSkeleton& p_skeleton, const char* p_name)
{
    if (p_name && p_name[0]) return p_skeleton.find(p_name);
    for (uint32_t i = 0; i < p_skeleton.joint_count(); ++i) {
        std::string lower = p_skeleton.names[i];
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, "head") != 0) continue;
        if (lower.find("end") != std::string::npos || lower.find("top") != std::string::npos) continue;
        return (int32_t)i;
    }
    return SKELETON_NO_JOINT;
}

}
