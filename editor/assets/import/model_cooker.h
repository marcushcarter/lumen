#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/import/mesh_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <core/assets/lmodel.h>
#include <filesystem>
#include <memory>

struct aiScene;

namespace lumen {

struct EditorContext;
struct Project;

struct ModelCooker
{
    static constexpr float READ_END = 0.1f;
    static constexpr float SAVE_SHARE = 0.02f;
    static constexpr uint64_t MESH_COST_PER_TRI = 8;

    struct Settings {
        std::wstring name;
        ModelImportMode mode = ModelImportMode::FULL;
        MeshCooker::CookSettings mesh_settings;
        TextureCooker::CookSettings texture_settings;
    };

    struct Job {
        const Project* project = nullptr;
        std::filesystem::path source;
        std::filesystem::path dst_dir;
        std::filesystem::path anim_dir;
        std::filesystem::path dst_lmodel;
        std::filesystem::path dst_lmesh;
        Guid model_guid;
        Guid mesh_guid;
        Settings settings;
        ImportSession* session = nullptr;
    };

    static Error _import(const Job& p_job);
    static Guid _import_skeleton(const Job& p_job, const aiScene* p_scene);
    static uint32_t _import_animations(const Job& p_job, const aiScene* p_scene, const std::filesystem::path& p_dir, Guid p_skeleton, bool p_notify);

    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir, const Settings& p_settings = {});
};

}
