#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lcollision.h>
#include <filesystem>

struct aiScene;

namespace lumen {

struct EditorContext;

struct CollisionCooker
{
    static constexpr float DEFAULT_ERROR = 0.02f;
    static constexpr float DEFAULT_MIN_FEATURE = 0.15f;
    static constexpr int MAX_HULL_VERTICES = 256;
    static constexpr float MIN_PRIMITIVE_SIZE = 0.001f;

    struct Settings {
        CollisionType type = CollisionType::MESH;
        float error = DEFAULT_ERROR;
        float min_feature = DEFAULT_MIN_FEATURE;
        bool exclude_masked = true;

        bool operator==(const Settings&) const = default;
    };

    struct Record {
        std::filesystem::path source;
        Guid mesh;
        Settings settings;
        MeshCooker::CookSettings transform;
    };

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lcollision;
        std::filesystem::path content_bin;
        Guid guid;
        Guid mesh;
        Settings settings;
        MeshCooker::CookSettings transform;
    };

    static bool build(const aiScene* p_scene, const Settings& p_settings, const MeshCooker::CookSettings& p_transform, LCollision& r_collision);
    static Error cook(const Job& p_job, LCollision& r_collision);
    static bool load_record(const std::filesystem::path& p_lcollision, Record& r_record);
    static std::filesystem::path find(const std::filesystem::path& p_folder, Guid p_guid);
    static Guid mesh_collision(const std::filesystem::path& p_lmesh);
    static bool load_for_mesh(const std::filesystem::path& p_lmesh, Settings& r_settings);
    static Error reimport(EditorContext& ctx, const std::filesystem::path& p_lcollision, const Settings* p_override = nullptr);
};

}
