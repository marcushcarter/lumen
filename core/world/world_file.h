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

struct World;

struct WorldFile
{
    static constexpr const char* SECTION = "world";
    static constexpr const char* PREFAB_SECTION = "prefab";
    static constexpr const char* KEY_ROOT = "root";
    static constexpr const char* KEY_ENVIRONMENT = "environment";
    static constexpr const char* KEY_PLAYER = "player";
    static constexpr const char* KEY_LEGACY_FOLDERS = "folders";
    static constexpr const char* KEY_LEGACY_FOLDER = "folder";
    static constexpr const char* KEY_ENTITIES = "entities";
    static constexpr const char* KEY_GUID = "guid";
    static constexpr const char* KEY_TAGS = "tags";

    Guid guid;
    Guid root;
    AssetType type = AssetType::WORLD;

    static Error read(const std::filesystem::path& p_path, World& r_world, bool p_editor, WorldFile& r_file);
    static Error write(const std::filesystem::path& p_path, World& r_world, const WorldFile& p_file);
    static Error read_text(std::string_view p_text, World& r_world, bool p_editor, WorldFile& r_file);
    static std::string write_text(World& r_world, const WorldFile& p_file);
    static uint64_t hash(World& r_world);
};

}
