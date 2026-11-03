#pragma once
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lskeleton.h>
#include <filesystem>
#include <string>

struct aiScene;

namespace lumen {

struct Project;

struct SkeletonCooker
{
    struct Match {
        Guid guid;
        std::filesystem::path path;
        std::string source;
    };

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lskeleton;
        std::filesystem::path content_bin;
    };

    static bool build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton);
    static Match find_matching(const Project& p_project, uint64_t p_fingerprint);
    static Error cook(const Job& p_job, const LSkeleton& p_skeleton);
};

}
