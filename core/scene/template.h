#pragma once
#include <core/scene/scene.h>
#include <core/base/error.h>
#include <toml++/toml.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct Template
{
    Guid guid;
    Guid root;
    std::filesystem::path path;
    Scene world;
    std::vector<Entity> entities;

    Entity root_entity() const { return world.find(root); }
    Guid target(Guid p_instance, Guid p_source) const;
};

struct TemplateLibrary
{
    static constexpr const char* EXTENSION = ".ltemplate";

    std::function<std::filesystem::path(Guid)> resolve;
    std::unordered_map<Guid, std::filesystem::path> paths;
    std::unordered_map<Guid, std::unique_ptr<Template>> cache;
    std::vector<Guid> _loading;
    uint64_t version = 0;

    void index(const std::filesystem::path& p_dir);
    void set_path(Guid p_template, const std::filesystem::path& p_path);
    std::filesystem::path path(Guid p_template) const;
    Template* get(Guid p_template);
    std::unique_ptr<Template> take(Guid p_template);
    void clear();
};

TemplateLibrary& template_library();

struct TemplateKeys
{
    static constexpr const char* OVERRIDES = "overrides";
    static constexpr const char* REMOVED_PARTS = "removed_parts";
    static constexpr const char* PART = "part";
    static constexpr const char* REMOVED = "removed";
    static constexpr const char* TAGS = "tags";
};

Guid template_part_guid(Guid p_instance, Guid p_source);
bool template_owned(const ComponentInfo& p_info);
Entity template_root_of(const Scene& p_world, Entity p_entity);
bool template_is_part(const Scene& p_world, Entity p_entity);
Template* template_for(const Scene& p_world, Entity p_entity);

void template_capture(Scene& r_world, Entity p_root, Template& r_template, toml::table& r_out);
void template_build(Scene& r_world, Entity p_root, Template& r_template, const toml::table* p_capture, bool p_editor);
void template_refresh(Scene& r_world, Guid p_template, Template* p_old, Template* p_new, bool p_editor);
Entity template_spawn(Scene& r_world, Template& r_template, Entity p_parent, const TransformComponent& p_transform);

uint64_t template_overrides(Scene& r_world, Entity p_entity, Template& r_template);
bool template_instance_overridden(Scene& r_world, Entity p_root, Template& r_template);
void template_overridden_fields(Scene& r_world, Entity p_entity, Template& r_template, uint32_t p_component, std::vector<std::string>& r_fields);
void template_revert_component(Scene& r_world, Entity p_entity, Template& r_template, uint32_t p_component);
void template_revert_entity(Scene& r_world, Entity p_entity, Template& r_template);
void template_revert_instance(Scene& r_world, Entity p_root, Template& r_template, bool p_editor);

Error template_create(Scene& r_world, const std::vector<Entity>& p_tops, const std::filesystem::path& p_path, Guid& r_template, Entity& r_instance);

}
