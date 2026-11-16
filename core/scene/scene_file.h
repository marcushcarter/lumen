#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <string_view>
#include <filesystem>
#include <string>
#include <vector>

namespace lumen {

struct Scene;

struct SceneFile
{
    static constexpr const char* EXTENSION = ".lscene";
    static constexpr const char* SECTION = "world";
    static constexpr const char* TEMPLATE_SECTION = "template";
    static constexpr const char* KEY_ROOT = "root";
    static constexpr const char* KEY_ENVIRONMENT = "environment";
    static constexpr const char* KEY_TERRAIN = "terrain";
    static constexpr const char* KEY_SHADOWS = "shadows";
    static constexpr const char* KEY_LEGACY_FOLDERS = "folders";
    static constexpr const char* KEY_LEGACY_FOLDER = "folder";
    static constexpr const char* KEY_ENTITIES = "entities";
    static constexpr const char* KEY_GUID = "guid";
    static constexpr const char* KEY_TAGS = "tags";

    Guid guid;
    Guid root;
    AssetType type = AssetType::SCENE;

    static Error read(const std::filesystem::path& p_path, Scene& r_world, bool p_editor, SceneFile& r_file);
    static Error write(const std::filesystem::path& p_path, Scene& r_world, const SceneFile& p_file);
    static Error read_text(std::string_view p_text, Scene& r_world, bool p_editor, SceneFile& r_file);
    static std::string write_text(Scene& r_world, const SceneFile& p_file);
    static uint64_t hash(Scene& r_world);
};

}
