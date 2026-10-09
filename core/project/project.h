#pragma once
#include <core/project/project_settings.h>
#include <core/base/error.h>
#include <core/assets/guid.h>
#include <filesystem>
#include <string>

namespace lumen {

struct Project
{
    static constexpr uint32_t FORMAT_VERSION = 1;
    
    static constexpr const char* FILE_NAME = "Data/project.config";
    static constexpr const char* FILE_ASSETDB = "Data/assetdb.bin";

    static constexpr const char* DIR_DATA = "Data";
    static constexpr const char* DIR_ASSETS = "Data/Assets";
    static constexpr const char* DIR_CONTENT = "Data/Content";

    static constexpr const char* ASSET_PATH_ROOT = "Assets";

    std::filesystem::path root;
    std::filesystem::path data_dir;
    std::filesystem::path assets_dir;
    std::filesystem::path content_dir;

    std::string name;

    ProjectSettings settings;

    void _resolve_dirs(const std::filesystem::path& p_root);
    static Error _ensure_layout(const std::filesystem::path& p_root);

    Error load(const std::filesystem::path& p_root);
    Error save() const;
    void unload();

    static Error create(const std::filesystem::path& p_root, std::string_view p_name);
    static Error destroy(const std::filesystem::path& p_root);

    static std::string peek_name(const std::filesystem::path& p_root);

    std::filesystem::path content_path(Guid p_guid) const;

    static std::string asset_path_from(const std::filesystem::path& p_assets_dir, const std::filesystem::path& p_path);
    static std::filesystem::path asset_path_resolve_from(const std::filesystem::path& p_assets_dir, std::string_view p_asset_path);

    std::string asset_path(const std::filesystem::path& p_path) const { return asset_path_from(assets_dir, p_path); }
    std::filesystem::path asset_path_resolve(std::string_view p_asset_path) const { return asset_path_resolve_from(assets_dir, p_asset_path); }


    bool loaded() const { return !root.empty(); }
};

}