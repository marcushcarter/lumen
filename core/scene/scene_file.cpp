#include <core/scene/scene_file.h>
#include <core/assets/asset_common.h>
#include <core/base/hash.h>
#include <core/io/file_io.h>
#include <core/scene/toml_archive.h>
#include <core/scene/component_serialize.h>
#include <core/scene/scene.h>
#include <core/scene/template.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <unordered_map>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lumen {

/****************/
/**** SHARED ****/
/****************/

static void _sorted_entities(const Scene& p_world, std::vector<Entity>& r_entities)
{
    r_entities.clear();
    p_world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) { r_entities.push_back(p_entity); });
    std::sort(r_entities.begin(), r_entities.end(), [](Entity a, Entity b) { return a.index < b.index; });
}

struct LegacyFolder
{
    Guid guid;
    int64_t parent = -1;
    std::string name;
    bool expanded = true;
};

static std::unordered_map<int64_t, LegacyFolder> _legacy_folders(const toml::array* p_folders)
{
    std::unordered_map<int64_t, LegacyFolder> out;
    if (!p_folders) return out;
    for (const toml::node& n : *p_folders) {
        const toml::table* t = n.as_table();
        if (!t) continue;
        LegacyFolder f;
        const int64_t index = (*t)["index"].value_or<int64_t>(-1);
        f.guid = Guid::from_string((*t)["guid"].value_or(std::string{}));
        if (!f.guid.valid()) f.guid = Guid::generate();
        f.parent = (*t)["parent"].value_or<int64_t>(-1);
        f.name = (*t)["name"].value_or(std::string("Folder"));
        f.expanded = (*t)["expanded"].value_or(true);
        if (index >= 0) out[index] = std::move(f);
    }
    return out;
}

static void _add_legacy_folders(Scene& r_world, const std::unordered_map<int64_t, LegacyFolder>& p_folders, bool p_editor)
{
    for (const auto& [index, f] : p_folders) {
        if (r_world.find(f.guid) != ENTITY_NULL) continue;
        const Entity e = r_world.create_persistent(f.guid);
        NameComponent name{};
        std::snprintf(name.name, NameComponent::MAX_LENGTH, "%s", f.name.c_str());
        r_world.add<NameComponent>(e, name);
        if (p_editor && f.expanded) r_world.add<EditorExpandedTag>(e);
        auto parent = p_folders.find(f.parent);
        if (parent != p_folders.end() && parent->first != index) r_world.add<ParentComponent>(e, { parent->second.guid });
    }
}

static void _write_entity(Scene& r_world, Entity p_entity, toml::table& r_table)
{
    r_table.insert_or_assign(SceneFile::KEY_GUID, r_world.try_get<EntityIdComponent>(p_entity)->guid.to_string());
    toml::array tags;
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || !info.has(r_world, p_entity)) continue;
        toml::table c;
        TomlWriter writer(c);
        info.serialize(r_world, p_entity, writer);
        if (c.empty()) tags.push_back(std::string(info.key));
        else r_table.insert_or_assign(info.key, std::move(c));
    }
    if (!tags.empty()) r_table.insert_or_assign(SceneFile::KEY_TAGS, std::move(tags));
}

static bool _tagged(const toml::array* p_tags, const char* p_key)
{
    return toml_has_string(p_tags, p_key);
}

static Template* _instance_template(const Scene& p_world, Entity p_entity)
{
    if (p_world.has<TemplatePartComponent>(p_entity)) return nullptr;
    const TemplateInstanceComponent* instance = p_world.try_get<TemplateInstanceComponent>(p_entity);
    return instance ? template_library().get(instance->tmpl) : nullptr;
}

static bool _skip_part(const Scene& p_world, Entity p_entity)
{
    const TemplatePartComponent* part = p_world.try_get<TemplatePartComponent>(p_entity);
    if (!part) return false;
    const Entity root = p_world.find(part->instance);
    return root != ENTITY_NULL && _instance_template(p_world, root) != nullptr;
}

static void _write_instance(Scene& r_world, Entity p_entity, Template& r_template, toml::table& r_table)
{
    r_table.insert_or_assign(SceneFile::KEY_GUID, r_world.try_get<EntityIdComponent>(p_entity)->guid.to_string());
    toml::array tags;
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || !template_owned(info) || !info.has(r_world, p_entity)) continue;
        toml::table c;
        TomlWriter writer(c);
        info.serialize(r_world, p_entity, writer);
        if (c.empty()) tags.push_back(std::string(info.key));
        else r_table.insert_or_assign(info.key, std::move(c));
    }
    if (!tags.empty()) r_table.insert_or_assign(SceneFile::KEY_TAGS, std::move(tags));
    template_capture(r_world, p_entity, r_template, r_table);
}

static void _read_entity(Scene& r_world, Entity p_entity, const toml::table& p_table, bool p_editor)
{
    const toml::array* tags = p_table.get_as<toml::array>(SceneFile::KEY_TAGS);
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || (info.editor_only && !p_editor)) continue;
        if (const toml::table* c = p_table.get_as<toml::table>(info.key)) {
            info.add_default(r_world, p_entity);
            TomlReader reader(*c);
            info.serialize(r_world, p_entity, reader);
        } else if (_tagged(tags, info.key)) {
            info.add_default(r_world, p_entity);
        }
    }
}

static void _hash_entity(Scene& r_world, Entity p_entity, HashWriter& r_writer)
{
    r_writer._value(r_world.try_get<EntityIdComponent>(p_entity)->guid.value);
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || !info.has(r_world, p_entity)) continue;
        r_writer._key(info.key);
        info.serialize(r_world, p_entity, r_writer);
    }
}

static std::string _text(const toml::table& p_table)
{
    std::ostringstream ss;
    ss << p_table;
    return ss.str();
}

static bool _parse(std::string_view p_text, toml::table& r_table, const char* p_what)
{
    try { r_table = toml::parse(p_text); }
    catch (const toml::parse_error& e) {
        log_write("Scene File: failed to parse %s (%s).", p_what, std::string(e.description()).c_str());
        return false;
    }
    return true;
}

/****************/
/**** PUBLIC ****/
/****************/

std::string SceneFile::write_text(Scene& r_world, const SceneFile& p_file)
{
    const bool tmpl = p_file.type == AssetType::TEMPLATE;
    toml::table section;

    if (!tmpl) {
        toml::table environment;
        TomlWriter env_writer(environment);
        serialize(env_writer, r_world.environment);
        section.insert_or_assign(KEY_ENVIRONMENT, std::move(environment));
    } else if (p_file.root.valid()) {
        section.insert_or_assign(KEY_ROOT, p_file.root.to_string());
    }

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    toml::array list;
    for (Entity e : entities) {
        if (_skip_part(r_world, e)) continue;
        toml::table t;
        if (Template* instance = _instance_template(r_world, e)) _write_instance(r_world, e, *instance, t);
        else _write_entity(r_world, e, t);
        list.push_back(std::move(t));
    }
    section.insert_or_assign(KEY_ENTITIES, std::move(list));

    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_file.guid.valid() ? p_file.guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(tmpl ? AssetType::TEMPLATE : AssetType::SCENE) } }},
        { std::string(tmpl ? TEMPLATE_SECTION : SECTION), std::move(section) },
    };
    return _text(tbl);
}

Error SceneFile::write(const std::filesystem::path& p_path, Scene& r_world, const SceneFile& p_file)
{
    return write_file_atomic(p_path, write_text(r_world, p_file)) ? Error::OK : Error::FAILED;
}

Error SceneFile::read_text(std::string_view p_text, Scene& r_world, bool p_editor, SceneFile& r_file)
{
    using enum Error;
    toml::table tbl;
    if (!_parse(p_text, tbl, "a world")) return FAILED;
    auto asset = tbl["asset"];
    const AssetType type = asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0));
    if (type != AssetType::SCENE && type != AssetType::TEMPLATE) return FAILED;

    r_file = SceneFile{};
    r_file.type = type;
    r_file.guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    const toml::table* section = tbl[type == AssetType::TEMPLATE ? TEMPLATE_SECTION : SECTION].as_table();
    if (!section) return OK;
    r_file.root = Guid::from_string(section->get_as<std::string>(KEY_ROOT) ? section->get_as<std::string>(KEY_ROOT)->get() : std::string{});

    if (const toml::table* environment = section->get_as<toml::table>(KEY_ENVIRONMENT)) {
        TomlReader reader(*environment);
        serialize(reader, r_world.environment);
    }

    const std::unordered_map<int64_t, LegacyFolder> legacy = _legacy_folders(section->get_as<toml::array>(KEY_LEGACY_FOLDERS));
    _add_legacy_folders(r_world, legacy, p_editor);

    const toml::array* list = section->get_as<toml::array>(KEY_ENTITIES);
    if (!list) return OK;
    uint32_t renamed = 0;
    std::vector<std::pair<Entity, const toml::table*>> instances;
    for (const toml::node& n : *list) {
        const toml::table* t = n.as_table();
        if (!t) continue;
        Guid guid = Guid::from_string((*t)[KEY_GUID].value_or(std::string{}));
        if (!guid.valid() || r_world.find(guid) != ENTITY_NULL) {
            guid = Guid::generate();
            renamed++;
        }
        const Entity e = r_world.create_persistent(guid);
        _read_entity(r_world, e, *t, p_editor);
        if (r_world.has<TemplateInstanceComponent>(e)) instances.emplace_back(e, t);
        if (const toml::table* old = t->get_as<toml::table>(KEY_LEGACY_FOLDER); old && !r_world.has<ParentComponent>(e)) {
            auto folder = legacy.find((*old)["folder"].value_or<int64_t>(-1));
            if (folder != legacy.end()) r_world.add<ParentComponent>(e, { folder->second.guid });
        }
    }
    for (const auto& [e, t] : instances) {
        Template* tmpl = template_library().get(r_world.try_get<TemplateInstanceComponent>(e)->tmpl);
        if (tmpl) template_build(r_world, e, *tmpl, t, p_editor);
    }
    if (!legacy.empty()) log_write("Scene File: converted %zu folders into folder entities.", legacy.size());
    if (renamed) log_write("Scene File: %u entities had a missing or duplicate guid and got a new one.", renamed);
    return OK;
}

Error SceneFile::read(const std::filesystem::path& p_path, Scene& r_world, bool p_editor, SceneFile& r_file)
{
    std::ifstream f(p_path, std::ios::binary);
    if (!f) {
        log_write("Scene File: couldn't open '%s'.", p_path.string().c_str());
        return Error::FAILED;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return read_text(ss.str(), r_world, p_editor, r_file);
}

uint64_t SceneFile::hash(Scene& r_world)
{
    HashWriter writer;
    serialize(writer, r_world.environment);

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    for (Entity e : entities) {
        if (_skip_part(r_world, e)) continue;
        Template* tmpl = _instance_template(r_world, e);
        if (!tmpl) {
            _hash_entity(r_world, e, writer);
            continue;
        }
        writer._value(r_world.try_get<EntityIdComponent>(e)->guid.value);
        for (const ComponentInfo& info : r_world.components) {
            if (!info.key || !template_owned(info) || !info.has(r_world, e)) continue;
            writer._key(info.key);
            info.serialize(r_world, e, writer);
        }
        toml::table capture;
        template_capture(r_world, e, *tmpl, capture);
        if (!capture.empty()) writer.hash = hash_fnv(_text(capture), writer.hash);
    }
    return writer.hash;
}

}
