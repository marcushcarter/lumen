#pragma once
#include <editor/assets/asset_import_tracker.h>
#include <filesystem>

namespace lumen {

struct Project;
struct EditorContext;

struct MeshCooker
{
    struct CookSettings {
        uint32_t dag_level = 2;
        float scale = 1.0f;
        uint32_t max_verts = 64;
        uint32_t max_tris = 124;
        uint32_t group_size = 8;
        uint32_t max_levels = 50;
    };

    // dag levels
    // 0 = one cluster, full res mesh
    // 1 = clusters, full re mesh
    // 2 = dag + clusters

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lmesh;
        std::filesystem::path content_bin;
        Guid guid;
        CookSettings settings;
        std::shared_ptr<ImportControl> progress;
    };
    
    static Error _cook(const Job& p_job);

    static Error import(const Project& p_project, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, Guid& r_guid, const CookSettings& p_settings = {});
    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const CookSettings& p_settings = {});
};

}