#pragma once
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lskeleton.h>
#include <filesystem>
#include <toml++/toml.hpp>

struct aiScene;

namespace lumen {

struct SkeletonCooker
{
    static bool build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton);
    static toml::table dynamics_table(const LSkeleton& p_skeleton);
    static uint32_t load_dynamics(const std::filesystem::path& p_lmesh, LSkeleton& r_skeleton);
    static Error save_dynamics(const std::filesystem::path& p_lmesh, const std::filesystem::path& p_content_bin, const LSkeleton& p_skeleton);
};

}
