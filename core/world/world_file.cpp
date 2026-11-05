#include <core/world/world_file.h>
#include <core/assets/asset_common.h>
#include <core/base/hash.h>
#include <core/io/file_io.h>
#include <core/world/archive.h>
#include <core/world/component_serialize.h>
#include <core/world/world.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace lumen {

/*****************/
/**** WRITING ****/
/*****************/

struct TomlWriter : Archive
{
    toml::table& table;

    explicit TomlWriter(toml::table& r_table) : table(r_table) {}

    void field(const char* p_key, bool& r_value) override { table.insert_or_assign(p_key, r_value); }
    void field(const char* p_key, int32_t& r_value) override { table.insert_or_assign(p_key, (int64_t)r_value); }
    void field(const char* p_key, uint32_t& r_value) override { table.insert_or_assign(p_key, (int64_t)r_value); }
    void field(const char* p_key, float& r_value) override { table.insert_or_assign(p_key, (double)r_value); }
    void field(const char* p_key, vec2& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y }); }
    void field(const char* p_key, vec3& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z }); }
    void field(const char* p_key, vec4& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z, (double)r_value.w }); }
    void field(const char* p_key, uvec3& r_value) override { table.insert_or_assign(p_key, toml::array{ (int64_t)r_value.x, (int64_t)r_value.y, (int64_t)r_value.z }); }
    void field(const char* p_key, quat& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z, (double)r_value.w }); }
    void field(const char* p_key, Guid& r_value) override { table.insert_or_assign(p_key, r_value.valid() ? r_value.to_string() : std::string{}); }
    void field(const char* p_key, std::string& r_value) override { table.insert_or_assign(p_key, r_value); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override { table.insert_or_assign(p_key, std::string(r_text, strnlen(r_text, p_capacity))); }
};

/*****************/
/**** READING ****/
/*****************/

struct TomlReader : Archive
{
    const toml::table& table;

    explicit TomlReader(const toml::table& p_table) : table(p_table) { loading = true; }

    template <typename T>
    void _number(const char* p_key, T& r_value)
    {
        if (const toml::node* n = table.get(p_key)) {
            if (n->is_integer()) r_value = (T)n->as_integer()->get();
            else if (n->is_floating_point()) r_value = (T)n->as_floating_point()->get();
        }
    }

    template <int N>
    bool _floats(const char* p_key, float (&r_out)[N])
    {
        const toml::array* list = table.get_as<toml::array>(p_key);
        if (!list || list->size() != N) return false;
        for (int i = 0; i < N; i++) {
            const toml::node& n = (*list)[i];
            if (n.is_floating_point()) r_out[i] = (float)n.as_floating_point()->get();
            else if (n.is_integer()) r_out[i] = (float)n.as_integer()->get();
            else return false;
        }
        return true;
    }

    void field(const char* p_key, bool& r_value) override { if (const toml::value<bool>* v = table.get_as<bool>(p_key)) r_value = v->get(); }
    void field(const char* p_key, int32_t& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, uint32_t& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, float& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, vec2& r_value) override { float f[2]; if (_floats(p_key, f)) r_value = vec2(f[0], f[1]); }
    void field(const char* p_key, vec3& r_value) override { float f[3]; if (_floats(p_key, f)) r_value = vec3(f[0], f[1], f[2]); }
    void field(const char* p_key, vec4& r_value) override { float f[4]; if (_floats(p_key, f)) r_value = vec4(f[0], f[1], f[2], f[3]); }
    void field(const char* p_key, uvec3& r_value) override { float f[3]; if (_floats(p_key, f)) r_value = uvec3((uint32_t)f[0], (uint32_t)f[1], (uint32_t)f[2]); }
    void field(const char* p_key, quat& r_value) override { float f[4]; if (_floats(p_key, f)) r_value = normalize(quat(f[3], f[0], f[1], f[2])); }
    void field(const char* p_key, Guid& r_value) override { if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) r_value = Guid::from_string(v->get()); }
    void field(const char* p_key, std::string& r_value) override { if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) r_value = v->get(); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override
    {
        if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) std::snprintf(r_text, p_capacity, "%s", v->get().c_str());
    }
};

/*****************/
/**** HASHING ****/
/*****************/

struct HashWriter : Archive
{
    uint64_t hash = HASH_FNV_OFFSET;

    void _key(const char* p_key) { hash = hash_fnv(p_key, hash); }
    void _value(uint64_t p_value) { hash = hash_fnv_step(hash, p_value); }
    void _float(float p_value) { _value(std::bit_cast<uint32_t>(p_value)); }

    void field(const char* p_key, bool& r_value) override { _key(p_key); _value(r_value); }
    void field(const char* p_key, int32_t& r_value) override { _key(p_key); _value((uint32_t)r_value); }
    void field(const char* p_key, uint32_t& r_value) override { _key(p_key); _value(r_value); }
    void field(const char* p_key, float& r_value) override { _key(p_key); _float(r_value); }
    void field(const char* p_key, vec2& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); }
    void field(const char* p_key, vec3& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); }
    void field(const char* p_key, vec4& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); _float(r_value.w); }
    void field(const char* p_key, uvec3& r_value) override { _key(p_key); _value(r_value.x); _value(r_value.y); _value(r_value.z); }
    void field(const char* p_key, quat& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); _float(r_value.w); }
    void field(const char* p_key, Guid& r_value) override { _key(p_key); _value(r_value.value); }
    void field(const char* p_key, std::string& r_value) override { _key(p_key); hash = hash_fnv(r_value, hash); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override { _key(p_key); hash = hash_fnv(std::string_view(r_text, strnlen(r_text, p_capacity)), hash); }
};

/****************/
/**** SHARED ****/
/****************/

static void _sorted_entities(const World& p_world, std::vector<Entity>& r_entities)
{
    r_entities.clear();
    p_world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) { r_entities.push_back(p_entity); });
    std::sort(r_entities.begin(), r_entities.end(), [](Entity a, Entity b) { return a.index < b.index; });
}

static void _serialize_folder(Archive& a, WorldFolder& r_folder)
{
    a.field("index", r_folder.index);
    a.field("guid", r_folder.guid);
    int32_t parent = r_folder.parent == WorldFolder::ROOT ? -1 : (int32_t)r_folder.parent;
    a.field("parent", parent);
    r_folder.parent = parent < 0 ? WorldFolder::ROOT : (uint32_t)parent;
    a.field("name", r_folder.name);
    a.field("expanded", r_folder.expanded);
}

/****************/
/**** PUBLIC ****/
/****************/

Error WorldFile::write(const std::filesystem::path& p_path, World& r_world, const WorldFile& p_file)
{
    using enum Error;
    toml::table section;

    toml::table environment;
    TomlWriter env_writer(environment);
    serialize(env_writer, r_world.environment);
    section.insert_or_assign(KEY_ENVIRONMENT, std::move(environment));

    toml::array folders;
    for (WorldFolder f : p_file.folders) {
        toml::table t;
        TomlWriter writer(t);
        _serialize_folder(writer, f);
        folders.push_back(std::move(t));
    }
    section.insert_or_assign(KEY_FOLDERS, std::move(folders));

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    toml::array list;
    for (Entity e : entities) {
        toml::table t;
        t.insert_or_assign(KEY_GUID, r_world.try_get<EntityIdComponent>(e)->guid.to_string());
        toml::array tags;
        for (const ComponentInfo& info : r_world.components) {
            if (!info.key || !info.has(r_world, e)) continue;
            toml::table c;
            TomlWriter writer(c);
            info.serialize(r_world, e, writer);
            if (c.empty()) tags.push_back(std::string(info.key));
            else t.insert_or_assign(info.key, std::move(c));
        }
        if (!tags.empty()) t.insert_or_assign(KEY_TAGS, std::move(tags));
        list.push_back(std::move(t));
    }
    section.insert_or_assign(KEY_ENTITIES, std::move(list));

    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_file.guid.valid() ? p_file.guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(AssetType::WORLD) } }},
        { std::string(SECTION), std::move(section) },
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

Error WorldFile::read(const std::filesystem::path& p_path, World& r_world, bool p_editor, WorldFile& r_file)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error& e) {
        log_write("World File: failed to parse '%s' (%s).", p_path.string().c_str(), std::string(e.description()).c_str());
        return FAILED;
    }
    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::WORLD) return FAILED;

    r_file = WorldFile{};
    r_file.guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    const toml::table* section = tbl[SECTION].as_table();
    if (!section) return OK;

    if (const toml::table* environment = section->get_as<toml::table>(KEY_ENVIRONMENT)) {
        TomlReader reader(*environment);
        serialize(reader, r_world.environment);
    }

    if (const toml::array* folders = section->get_as<toml::array>(KEY_FOLDERS); folders && p_editor) {
        for (const toml::node& n : *folders) {
            const toml::table* t = n.as_table();
            if (!t) continue;
            WorldFolder f;
            TomlReader reader(*t);
            _serialize_folder(reader, f);
            r_file.folders.push_back(std::move(f));
        }
    }

    const toml::array* list = section->get_as<toml::array>(KEY_ENTITIES);
    if (!list) return OK;
    uint32_t renamed = 0;
    for (const toml::node& n : *list) {
        const toml::table* t = n.as_table();
        if (!t) continue;
        Guid guid = Guid::from_string((*t)[KEY_GUID].value_or(std::string{}));
        if (!guid.valid() || r_world.find(guid) != ENTITY_NULL) {
            guid = Guid::generate();
            renamed++;
        }
        const Entity e = r_world.create_persistent(guid);
        const toml::array* tags = t->get_as<toml::array>(KEY_TAGS);
        for (const ComponentInfo& info : r_world.components) {
            if (!info.key || (info.editor_only && !p_editor)) continue;
            if (const toml::table* c = t->get_as<toml::table>(info.key)) {
                info.add_default(r_world, e);
                TomlReader reader(*c);
                info.serialize(r_world, e, reader);
            } else if (tags) {
                for (const toml::node& tag : *tags) {
                    if (tag.value<std::string>() == info.key) info.add_default(r_world, e);
                }
            }
        }
    }
    if (renamed) log_write("World File: %u entities in '%s' had a missing or duplicate guid and got a new one.", renamed, p_path.string().c_str());
    return OK;
}

uint64_t WorldFile::hash(World& r_world, const std::vector<WorldFolder>& p_folders)
{
    HashWriter writer;
    serialize(writer, r_world.environment);
    for (WorldFolder f : p_folders) _serialize_folder(writer, f);

    std::vector<Entity> entities;
    _sorted_entities(r_world, entities);
    for (Entity e : entities) {
        writer._value(r_world.try_get<EntityIdComponent>(e)->guid.value);
        for (const ComponentInfo& info : r_world.components) {
            if (!info.key || !info.has(r_world, e)) continue;
            writer._key(info.key);
            info.serialize(r_world, e, writer);
        }
    }
    return writer.hash;
}

}
