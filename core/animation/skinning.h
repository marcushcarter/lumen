#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
#include <vector>

namespace lumen {

void rig_joint_map(const LSkeleton& p_skeleton, const LMeshRig& p_rig, std::vector<int32_t>& r_map);
int32_t skeleton_find_head(const LSkeleton& p_skeleton, const char* p_name);
void rig_skin(const std::vector<mat4>& p_skeleton_global, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, std::vector<mat4>& r_skin);

}
