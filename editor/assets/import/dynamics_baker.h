#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
#include <core/base/error.h>
#include <filesystem>
#include <vector>

namespace lumen {

void dynamics_fit_colliders(const std::vector<vec3>& p_positions, const std::vector<SkinVertex>& p_skin, uint32_t p_joint_count, std::vector<LMeshCollider>& r_out);
bool dynamics_strip_cloths(LMeshData& r_data);
uint32_t dynamics_bake_cloths(LMeshData& r_data, const LSkeleton& p_skeleton);
Error dynamics_rebake_cloths(const std::filesystem::path& p_bin, const LSkeleton& p_skeleton);

}
