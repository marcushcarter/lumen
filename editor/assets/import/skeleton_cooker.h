#pragma once
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lskeleton.h>
#include <filesystem>

struct aiScene;

namespace lumen {

struct SkeletonCooker
{
    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lskeleton;
        std::filesystem::path content_bin;
    };

    // False when the scene has no bones or more joints than SkinVertex can index.
    static bool build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton);
    static Error cook(const Job& p_job, const LSkeleton& p_skeleton);
};

}
