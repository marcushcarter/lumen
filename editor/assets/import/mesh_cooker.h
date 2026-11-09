#pragma once
#include <core/assets/lcollision.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <core/assets/lmesh.h>
#include <assimp/ProgressHandler.hpp>
#include <filesystem>
#include <vector>

struct aiScene;

namespace lumen {

struct Project;
struct EditorContext;

struct AssimpImportProgress : Assimp::ProgressHandler
{
    ImportStep step;

    bool Update(float p_percentage) override {
        if (p_percentage >= 0.0f) step.report(p_percentage);
        return !step.cancelled();
    }
};

struct CollisionSettings
{
    static constexpr float DEFAULT_ERROR = 0.02f;
    static constexpr float DEFAULT_MIN_FEATURE = 0.15f;

    CollisionType type = CollisionType::MESH;
    float error = DEFAULT_ERROR;
    float min_feature = DEFAULT_MIN_FEATURE;
    bool exclude_masked = true;

    bool operator==(const CollisionSettings&) const = default;
};

struct MeshCooker
{
    static constexpr float GATHER_SHARE = 0.1f;
    static constexpr float BUILD_END = 0.92f;

    struct CookSettings {
        MeshDagLevel dag_level = MeshDagLevel::CLUSTERED_DAG;
        vec3 translation = vec3(0.0f);
        vec3 rotation = vec3(0.0f);
        float scale = 1.0f;
        bool load_skeleton = true;
        bool generate_collision = true;
        CollisionSettings collision;
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
        Guid skeleton;
        std::vector<uint8_t> collision;
        ImportStep step;
    };

    static unsigned import_flags();
    static bool load_settings(const std::filesystem::path& p_lmesh, CookSettings& r_settings, std::filesystem::path& r_source);
    static std::vector<uint32_t> material_slots(const aiScene* p_scene);
    static Error cook_scene(const Job& p_job, const aiScene* p_scene);
};

}
