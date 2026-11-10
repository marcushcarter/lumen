#include <core/world/world_file.h>
#include <core/assets/asset_common.h>
#include <core/base/hash.h>
#include <core/io/file_io.h>
#include <core/world/toml_archive.h>
#include <core/world/component_serialize.h>
#include <core/world/world.h>
#include <core/world/prefab.h>
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

static void _sorted_entities(const World& p_world, std::vector<Entity>& r_entities)
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

static void _add_legacy_folders(World& r_world, const std::unordered_map<int64_t, LegacyFolder>& p_folders, bool p_editor)
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

static void _write_entity(World& r_world, Entity p_entity, toml::table& r_table)
{
    r_table.insert_or_assign(WorldFile::KEY_GUID, r_world.try_get<EntityIdComponent>(p_entity)->guid.to_string());
    toml::array tags;
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || !info.has(r_world, p_entity)) continue;
        toml::table c;
        TomlWriter writer(c);
        info.serialize(r_world, p_entity, writer);
        if (c.empty()) tags.push_back(std::string(info.key));
        else r_table.insert_or_assign(info.key, std::move(c));
    }
    if (!tags.empty()) r_table.insert_or_assign(WorldFile::KEY_TAGS, std::move(tags));
}

static bool _tagged(const toml::array* p_tags, const char* p_key)
{
    return toml_has_string(p_tags, p_key);
}

static Prefab* _instance_prefab(const World& p_world, Entity p_entity)
{
    if (p_world.has<PrefabPartComponent>(p_entity)) return nullptr;
    const PrefabInstanceComponent* instance = p_world.try_get<PrefabInstanceComponent>(p_entity);
    return instance ? prefab_library().get(instance->prefab) : nullptr;
}

static bool _skip_part(const World& p_world, Entity p_entity)
{
    const PrefabPartComponent* part = p_world.try_get<PrefabPartComponent>(p_entity);
    if (!part) return false;
    const Entity root = p_world.find(part->instance);
    return root != ENTITY_NULL && _instance_prefab(p_world, root) != nullptr;
}

static void _write_instance(World& r_world, Entity p_entity, Prefab& r_prefab, toml::table& r_table)
{
    r_table.insert_or_assign(WorldFile::KEY_GUID, r_world.try_get<EntityIdComponent>(p_entity)->guid.to_string());
    toml::array tags;
    for (const ComponentInfo& info : r_world.components) {
        if (!info.key || !prefab_owned(info) || !info.has(r_world, p_entity)) continue;
        toml::table c;
        TomlWriter writer(c);
        info.serialize(r_world, p_entity, writer);
        if (c.empty()) tags.push_back(std::string(info.key));
        else r_table.insert_or_assign(info.key, std::move(c));
    }
    if (!tags.empty()) r_table.insert_or_assign(WorldFile::KEY_TAGS, std::move(tags));
    prefab_capture(r_world, p_entity, r_prefab, r_table);
}

static void _read_entity(World& r_world, Entity p_entity, const toml::table& p_table, bool p_editor)
{
    const toml::array* tags = p_table.get_as<toml::array>(WorldFile::KEY_TAGS);
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

static void _hash_entity(World& r_world, Entity p_entity, HashWriter& r_writer)
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
        log_write("World File: failed to parse %s (%s).", p_what, std::string(e.description()).c_str());
        return false;
    }
    return true;
}

/****************/
/**** PUBLIC ****/
/****************/

std::string WorldFile::write_text(World& r_world, const WorldFile& p_file)
{
    const bool prefab = p_file.type == AssetType::PREFAB;
    toml::table section;

    if (!prefab) {
        toml::table environment;
        TomlWriter env_writer(environment);
        serialize(env_writer, r_world.environment);
        section.insert_or_assign(KEY_ENVIRONMENT, std::move(environment));
        toml::table player;
        TomlWriter player_writer(player);
        serialize(player_writer, r_world.player);
        section.insert_or_assign(KEY_PLAYER, std::move(player));
    } else if (p_file.root.valid()) {
        section.insert_or_assign(KEY_ROOT, p_file.root.to_string());
    }

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    toml::array list;
    for (Entity e : entities) {
        if (_skip_part(r_world, e)) continue;
        toml::table t;
        if (Prefab* instance = _instance_prefab(r_world, e)) _write_instance(r_world, e, *instance, t);
        else _write_entity(r_world, e, t);
        list.push_back(std::move(t));
    }
    section.insert_or_assign(KEY_ENTITIES, std::move(list));

    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_file.guid.valid() ? p_file.guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(prefab ? AssetType::PREFAB : AssetType::WORLD) } }},
        { std::string(prefab ? PREFAB_SECTION : SECTION), std::move(section) },
    };
    return _text(tbl);
}

Error WorldFile::write(const std::filesystem::path& p_path, World& r_world, const WorldFile& p_file)
{
    return write_file_atomic(p_path, write_text(r_world, p_file)) ? Error::OK : Error::FAILED;
}

Error WorldFile::read_text(std::string_view p_text, World& r_world, bool p_editor, WorldFile& r_file)
{
    using enum Error;
    toml::table tbl;
    if (!_parse(p_text, tbl, "a world")) return FAILED;
    auto asset = tbl["asset"];
    const AssetType type = asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0));
    if (type != AssetType::WORLD && type != AssetType::PREFAB) return FAILED;

    r_file = WorldFile{};
    r_file.type = type;
    r_file.guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    const toml::table* section = tbl[type == AssetType::PREFAB ? PREFAB_SECTION : SECTION].as_table();
    if (!section) return OK;
    r_file.root = Guid::from_string(section->get_as<std::string>(KEY_ROOT) ? section->get_as<std::string>(KEY_ROOT)->get() : std::string{});

    if (const toml::table* environment = section->get_as<toml::table>(KEY_ENVIRONMENT)) {
        TomlReader reader(*environment);
        serialize(reader, r_world.environment);
    }
    if (const toml::table* player = section->get_as<toml::table>(KEY_PLAYER)) {
        TomlReader reader(*player);
        serialize(reader, r_world.player);
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
        if (r_world.has<PrefabInstanceComponent>(e)) instances.emplace_back(e, t);
        if (const toml::table* old = t->get_as<toml::table>(KEY_LEGACY_FOLDER); old && !r_world.has<ParentComponent>(e)) {
            auto folder = legacy.find((*old)["folder"].value_or<int64_t>(-1));
            if (folder != legacy.end()) r_world.add<ParentComponent>(e, { folder->second.guid });
        }
    }
    for (const auto& [e, t] : instances) {
        Prefab* prefab = prefab_library().get(r_world.try_get<PrefabInstanceComponent>(e)->prefab);
        if (prefab) prefab_build(r_world, e, *prefab, t, p_editor);
    }
    if (!legacy.empty()) log_write("World File: converted %zu folders into folder entities.", legacy.size());
    if (renamed) log_write("World File: %u entities had a missing or duplicate guid and got a new one.", renamed);
    return OK;
}

Error WorldFile::read(const std::filesystem::path& p_path, World& r_world, bool p_editor, WorldFile& r_file)
{
    std::ifstream f(p_path, std::ios::binary);
    if (!f) {
        log_write("World File: couldn't open '%s'.", p_path.string().c_str());
        return Error::FAILED;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return read_text(ss.str(), r_world, p_editor, r_file);
}

uint64_t WorldFile::hash(World& r_world)
{
    HashWriter writer;
    serialize(writer, r_world.environment);
    serialize(writer, r_world.player);

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    for (Entity e : entities) {
        if (_skip_part(r_world, e)) continue;
        Prefab* prefab = _instance_prefab(r_world, e);
        if (!prefab) {
            _hash_entity(r_world, e, writer);
            continue;
        }
        writer._value(r_world.try_get<EntityIdComponent>(e)->guid.value);
        for (const ComponentInfo& info : r_world.components) {
            if (!info.key || !prefab_owned(info) || !info.has(r_world, e)) continue;
            writer._key(info.key);
            info.serialize(r_world, e, writer);
        }
        toml::table capture;
        prefab_capture(r_world, e, *prefab, capture);
        if (!capture.empty()) writer.hash = hash_fnv(_text(capture), writer.hash);
    }
    return writer.hash;
}

}
