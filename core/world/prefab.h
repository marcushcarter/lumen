#pragma once
#include <core/world/world.h>
#include <core/base/error.h>
#include <toml++/toml.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct Prefab
{
    Guid guid;
    Guid root;
    std::filesystem::path path;
    World world;
    std::vector<Entity> entities;

    Entity root_entity() const { return world.find(root); }
    Guid target(Guid p_instance, Guid p_source) const;
};

struct PrefabLibrary
{
    static constexpr const char* EXTENSION = ".lprefab";

    std::function<std::filesystem::path(Guid)> resolve;
    std::unordered_map<Guid, std::filesystem::path> paths;
    std::unordered_map<Guid, std::unique_ptr<Prefab>> cache;
    std::vector<Guid> _loading;
    uint64_t version = 0;

    void index(const std::filesystem::path& p_dir);
    void set_path(Guid p_prefab, const std::filesystem::path& p_path);
    std::filesystem::path path(Guid p_prefab) const;
    Prefab* get(Guid p_prefab);
    std::unique_ptr<Prefab> take(Guid p_prefab);
    void clear();
};

PrefabLibrary& prefab_library();

struct PrefabKeys
{
    static constexpr const char* OVERRIDES = "overrides";
    static constexpr const char* REMOVED_PARTS = "removed_parts";
    static constexpr const char* PART = "part";
    static constexpr const char* REMOVED = "removed";
    static constexpr const char* TAGS = "tags";
};

Guid prefab_part_guid(Guid p_instance, Guid p_source);
bool prefab_owned(const ComponentInfo& p_info);
Entity prefab_root_of(const World& p_world, Entity p_entity);
bool prefab_is_part(const World& p_world, Entity p_entity);
Prefab* prefab_for(const World& p_world, Entity p_entity);

void prefab_capture(World& r_world, Entity p_root, Prefab& r_prefab, toml::table& r_out);
void prefab_build(World& r_world, Entity p_root, Prefab& r_prefab, const toml::table* p_capture, bool p_editor);
void prefab_refresh(World& r_world, Guid p_prefab, Prefab* p_old, Prefab* p_new, bool p_editor);
Entity prefab_spawn(World& r_world, Prefab& r_prefab, Entity p_parent, const TransformComponent& p_transform);

uint64_t prefab_overrides(World& r_world, Entity p_entity, Prefab& r_prefab);
bool prefab_instance_overridden(World& r_world, Entity p_root, Prefab& r_prefab);
void prefab_overridden_fields(World& r_world, Entity p_entity, Prefab& r_prefab, uint32_t p_component, std::vector<std::string>& r_fields);
void prefab_revert_component(World& r_world, Entity p_entity, Prefab& r_prefab, uint32_t p_component);
void prefab_revert_entity(World& r_world, Entity p_entity, Prefab& r_prefab);
void prefab_revert_instance(World& r_world, Entity p_root, Prefab& r_prefab, bool p_editor);

Error prefab_create(World& r_world, const std::vector<Entity>& p_tops, const std::filesystem::path& p_path, Guid& r_prefab, Entity& r_instance);

}
