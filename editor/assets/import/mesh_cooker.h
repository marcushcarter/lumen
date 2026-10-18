#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <core/assets/lmesh.h>
#include <filesystem>

struct aiScene;

namespace lumen {

struct Project;
struct EditorContext;

struct MeshCooker
{
    struct CookSettings {
        MeshDagLevel dag_level = MeshDagLevel::CLUSTERED_DAG;
        float scale = 1.0f;
        uint32_t max_verts = 64;
        uint32_t max_tris = 64;
        uint32_t group_size = 8;
        uint32_t max_levels = 10;
    };

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lmesh;
        std::filesystem::path content_bin;
        Guid guid;
        CookSettings settings;
        std::shared_ptr<ImportControl> progress;
    };
    
    static unsigned import_flags();
    static Error _cook(const Job& p_job);
    static Error _cook_scene(const Job& p_job, const aiScene* p_scene);

    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const CookSettings& p_settings = {});
};

}