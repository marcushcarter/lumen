#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/import/mesh_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <core/assets/lmodel.h>
#include <filesystem>
#include <memory>

namespace lumen {

struct EditorContext;
struct Project;

struct ModelImporter
{
    struct Settings {
        ModelImportMode mode = ModelImportMode::FULL;
        MeshCooker::CookSettings mesh_settings;
        TextureCooker::CookSettings texture_settings;
    };

    struct Job {
        const Project* project = nullptr;
        std::filesystem::path source;
        std::filesystem::path dst_dir;
        std::filesystem::path dst_lmodel;
        std::filesystem::path dst_lmesh;
        std::filesystem::path mesh_content_bin;
        Guid model_guid;
        Guid mesh_guid;
        Settings settings;
        std::shared_ptr<ImportControl> progress;
    };

    static Error _import(const Job& p_job);

    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir, const Settings& p_settings = {});
};

}
