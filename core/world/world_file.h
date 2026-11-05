#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <string_view>
#include <filesystem>
#include <string>
#include <vector>

namespace lumen {

struct World;
struct Entity;

struct WorldFolder
{
    static constexpr uint32_t ROOT = 0xFFFFFFFF;

    uint32_t index = 0;
    Guid guid;
    uint32_t parent = ROOT;
    std::string name;
    bool expanded = true;

    bool operator==(const WorldFolder&) const = default;
};

struct WorldFile
{
    static constexpr const char* SECTION = "world";
    static constexpr const char* KEY_ENVIRONMENT = "environment";
    static constexpr const char* KEY_FOLDERS = "folders";
    static constexpr const char* KEY_ENTITIES = "entities";
    static constexpr const char* KEY_GUID = "guid";
    static constexpr const char* KEY_TAGS = "tags";

    Guid guid;
    std::vector<WorldFolder> folders;

    static Error read(const std::filesystem::path& p_path, World& r_world, bool p_editor, WorldFile& r_file);
    static Error write(const std::filesystem::path& p_path, World& r_world, const WorldFile& p_file);
    static Error read_text(std::string_view p_text, World& r_world, bool p_editor, WorldFile& r_file);
    static std::string write_text(World& r_world, const WorldFile& p_file);
    static uint64_t hash(World& r_world, const std::vector<WorldFolder>& p_folders);

    static std::string entity_text(World& r_world, Entity p_entity);
    static Error apply_entity_text(std::string_view p_text, World& r_world, bool p_editor);
    static uint64_t entity_hash(World& r_world, Entity p_entity);
    static std::string environment_text(World& r_world);
    static Error apply_environment_text(std::string_view p_text, World& r_world);
    static uint64_t environment_hash(World& r_world);
};

}
