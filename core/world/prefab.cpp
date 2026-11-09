#include <core/world/prefab.h>
#include <core/world/world_file.h>
#include <core/world/toml_archive.h>
#include <core/assets/asset_common.h>
#include <core/base/hash.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace lumen {

/*****************/
/**** LIBRARY ****/
/*****************/

PrefabLibrary& prefab_library()
{
    static PrefabLibrary library;
    return library;
}

void PrefabLibrary::index(const std::filesystem::path& p_dir)
{
    std::error_code ec;
    if (p_dir.empty() || !std::filesystem::is_directory(p_dir, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(p_dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != EXTENSION) continue;
        const AssetInfo info = read_asset_info(it->path());
        if (info.valid() && info.type == AssetType::PREFAB) paths[info.guid] = it->path();
    }
}

void PrefabLibrary::set_path(Guid p_prefab, const std::filesystem::path& p_path)
{
    paths[p_prefab] = p_path;
}

std::filesystem::path PrefabLibrary::path(Guid p_prefab) const
{
    if (resolve) {
        std::filesystem::path found = resolve(p_prefab);
        if (!found.empty()) return found;
    }
    auto it = paths.find(p_prefab);
    return it == paths.end() ? std::filesystem::path{} : it->second;
}

static void _finish(Prefab& r_prefab, Guid p_root)
{
    World& world = r_prefab.world;
    std::vector<Entity> roots;
    world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
        if (world.parent(p_entity) == ENTITY_NULL) roots.push_back(p_entity);
    });
    std::sort(roots.begin(), roots.end(), [](Entity a, Entity b) { return a.index < b.index; });
    if (roots.empty()) return;
    Entity root = roots.front();
    const Entity preferred = world.find(p_root);
    if (preferred != ENTITY_NULL && world.parent(preferred) == ENTITY_NULL) root = preferred;
    r_prefab.root = world.try_get<EntityIdComponent>(root)->guid;
    for (const Entity e : roots) {
        if (e != root) world.add<ParentComponent>(e, { r_prefab.root });
    }
    world.subtree(root, r_prefab.entities);
}

Prefab* PrefabLibrary::get(Guid p_prefab)
{
    if (!p_prefab.valid()) return nullptr;
    if (auto it = cache.find(p_prefab); it != cache.end()) return it->second.get();
    if (std::find(_loading.begin(), _loading.end(), p_prefab) != _loading.end()) {
        log_write("Prefab: %s contains itself, skipping the nested copy.", p_prefab.to_string().c_str());
        return nullptr;
    }
    const std::filesystem::path file = path(p_prefab);
    if (file.empty()) {
        log_write("Prefab: no prefab with guid %s.", p_prefab.to_string().c_str());
        cache.emplace(p_prefab, nullptr);
        return nullptr;
    }

    auto prefab = std::make_unique<Prefab>();
    prefab->guid = p_prefab;
    prefab->path = file;
    prefab->world.initialize();
    _loading.push_back(p_prefab);
    WorldFile wf;
    const Error err = WorldFile::read(file, prefab->world, false, wf);
    _loading.pop_back();
    if (err != Error::OK || wf.type != AssetType::PREFAB) {
        log_write("Prefab: failed to load '%s'.", file.string().c_str());
        cache.emplace(p_prefab, nullptr);
        return nullptr;
    }
    _finish(*prefab, wf.root);
    Prefab* out = prefab.get();
    cache.emplace(p_prefab, std::move(prefab));
    version++;
    return out;
}

std::unique_ptr<Prefab> PrefabLibrary::take(Guid p_prefab)
{
    auto it = cache.find(p_prefab);
    if (it == cache.end()) return nullptr;
    std::unique_ptr<Prefab> out = std::move(it->second);
    cache.erase(it);
    version++;
    return out;
}

void PrefabLibrary::clear()
{
    cache.clear();
    paths.clear();
    version++;
}

/*****************/
/**** HELPERS ****/
/*****************/

Guid prefab_part_guid(Guid p_instance, Guid p_source)
{
    Guid g{ hash_splitmix(hash_combine(hash_splitmix(p_instance.value), p_source.value)) };
    if (!g.valid()) g.value = 1;
    return g;
}

Guid Prefab::target(Guid p_instance, Guid p_source) const
{
    return p_source == root ? p_instance : prefab_part_guid(p_instance, p_source);
}

static bool _key_is(const ComponentInfo& p_info, const char* p_key)
{
    return p_info.key && std::strcmp(p_info.key, p_key) == 0;
}

static bool _saved(const ComponentInfo& p_info)
{
    return p_info.key && p_info.sync && p_info.serialize;
}

bool prefab_owned(const ComponentInfo& p_info)
{
    return p_info.editor_only || _key_is(p_info, "transform") || _key_is(p_info, "parent") || _key_is(p_info, "name") || _key_is(p_info, "prefab");
}

static Guid _guid(const World& p_world, Entity p_entity)
{
    const EntityIdComponent* id = p_world.try_get<EntityIdComponent>(p_entity);
    return id ? id->guid : Guid{};
}

Entity prefab_root_of(const World& p_world, Entity p_entity)
{
    if (const PrefabPartComponent* part = p_world.try_get<PrefabPartComponent>(p_entity)) return p_world.find(part->instance);
    if (p_world.has<PrefabInstanceComponent>(p_entity)) return p_entity;
    return ENTITY_NULL;
}

bool prefab_is_part(const World& p_world, Entity p_entity)
{
    return p_world.has<PrefabPartComponent>(p_entity);
}

Prefab* prefab_for(const World& p_world, Entity p_entity)
{
    const Entity root = prefab_root_of(p_world, p_entity);
    const PrefabInstanceComponent* instance = p_world.try_get<PrefabInstanceComponent>(root);
    return instance ? prefab_library().get(instance->prefab) : nullptr;
}

struct FieldHasher : Archive
{
    std::vector<std::pair<std::string, uint64_t>> fields;

    template <typename T>
    void _put(const char* p_key, T& r_value)
    {
        HashWriter writer;
        writer.field(p_key, r_value);
        fields.emplace_back(p_key, writer.hash);
    }

    void field(const char* p_key, bool& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, int32_t& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, uint32_t& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, float& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, vec2& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, vec3& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, vec4& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, uvec3& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, quat& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, Guid& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, std::string& r_value) override { _put(p_key, r_value); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override
    {
        HashWriter writer;
        writer.field(p_key, r_text, p_capacity);
        fields.emplace_back(p_key, writer.hash);
    }
};

static void _changed_fields(const ComponentInfo& p_info, World& r_base, Entity p_base, World& r_world, Entity p_entity, std::vector<std::string>& r_fields)
{
    r_fields.clear();
    FieldHasher base;
    FieldHasher mine;
    p_info.serialize(r_base, p_base, base);
    p_info.serialize(r_world, p_entity, mine);
    for (size_t i = 0; i < mine.fields.size(); i++) {
        const bool same = i < base.fields.size() && base.fields[i].first == mine.fields[i].first && base.fields[i].second == mine.fields[i].second;
        if (!same) r_fields.push_back(mine.fields[i].first);
    }
}

static bool _equal(const ComponentInfo& p_info, World& r_a, Entity p_a, World& r_b, Entity p_b)
{
    const bool has_a = p_info.has(r_a, p_a);
    const bool has_b = p_info.has(r_b, p_b);
    if (has_a != has_b) return false;
    if (!has_a) return true;
    std::vector<std::string> fields;
    _changed_fields(p_info, r_a, p_a, r_b, p_b, fields);
    return fields.empty();
}

static Guid _template_parent(const Prefab& p_prefab, Entity p_template, Guid p_instance)
{
    const ParentComponent* parent = p_prefab.world.try_get<ParentComponent>(p_template);
    return parent && parent->parent.valid() ? p_prefab.target(p_instance, parent->parent) : Guid{};
}

static Guid _parent_guid(const World& p_world, Entity p_entity)
{
    const ParentComponent* parent = p_world.try_get<ParentComponent>(p_entity);
    return parent ? parent->parent : Guid{};
}

static void _set_parent_guid(World& r_world, Entity p_entity, Guid p_parent)
{
    if (_parent_guid(r_world, p_entity) == p_parent) return;
    if (p_parent.valid()) r_world.add<ParentComponent>(p_entity, { p_parent });
    else r_world.remove<ParentComponent>(p_entity);
    r_world.touch(p_entity);
}

struct PrefabSlot
{
    Entity entity = ENTITY_NULL;
    Entity source = ENTITY_NULL;
    Guid instance;
    bool root = false;
};

static PrefabSlot _slot(World& r_world, Entity p_entity, Prefab& r_prefab)
{
    PrefabSlot slot;
    slot.entity = p_entity;
    if (const PrefabPartComponent* part = r_world.try_get<PrefabPartComponent>(p_entity)) {
        slot.instance = part->instance;
        slot.source = r_prefab.world.find(part->source);
    } else if (r_world.has<PrefabInstanceComponent>(p_entity)) {
        slot.instance = _guid(r_world, p_entity);
        slot.source = r_prefab.root_entity();
        slot.root = true;
    }
    return slot;
}

static bool _overridden(World& r_world, const PrefabSlot& p_slot, Prefab& r_prefab, const ComponentInfo& p_info)
{
    if (!_saved(p_info) || p_info.editor_only || (p_slot.root && prefab_owned(p_info))) return false;
    if (_key_is(p_info, "parent")) return _parent_guid(r_world, p_slot.entity) != _template_parent(r_prefab, p_slot.source, p_slot.instance);
    return !_equal(p_info, r_prefab.world, p_slot.source, r_world, p_slot.entity);
}

static void _write_component(const ComponentInfo& p_info, World& r_world, Entity p_entity, toml::table& r_record, toml::array& r_tags, const std::vector<std::string>* p_only = nullptr)
{
    toml::table c;
    TomlWriter writer(c);
    p_info.serialize(r_world, p_entity, writer);
    if (p_only) {
        std::vector<std::string> drop;
        for (const auto& [key, node] : c) {
            if (std::find(p_only->begin(), p_only->end(), std::string(key.str())) == p_only->end()) drop.emplace_back(key.str());
        }
        for (const std::string& key : drop) c.erase(key);
    }
    if (c.empty()) r_tags.push_back(std::string(p_info.key));
    else r_record.insert_or_assign(p_info.key, std::move(c));
}

static void _apply_record(World& r_world, Entity p_entity, const toml::table& p_record, bool p_editor)
{
    const toml::array* tags = p_record.get_as<toml::array>(PrefabKeys::TAGS);
    const toml::array* removed = p_record.get_as<toml::array>(PrefabKeys::REMOVED);
    for (const ComponentInfo& info : r_world.components) {
        if (!_saved(info) || (info.editor_only && !p_editor)) continue;
        if (const toml::table* c = p_record.get_as<toml::table>(info.key)) {
            if (!info.has(r_world, p_entity)) info.add_default(r_world, p_entity);
            TomlReader reader(*c);
            info.serialize(r_world, p_entity, reader);
            r_world.touch(p_entity);
        } else if (toml_has_string(tags, info.key)) {
            if (!info.has(r_world, p_entity)) info.add_default(r_world, p_entity);
        } else if (toml_has_string(removed, info.key)) {
            info.remove(r_world, p_entity);
        }
    }
}

static void _instance_parts(const World& p_world, Guid p_instance, std::vector<Entity>& r_parts)
{
    r_parts.clear();
    p_world.view<PrefabPartComponent>([&](Entity p_entity, const PrefabPartComponent& p_part) {
        if (p_part.instance == p_instance) r_parts.push_back(p_entity);
    });
}

/*******************/
/**** INSTANCES ****/
/*******************/

void prefab_capture(World& r_world, Entity p_root, Prefab& r_prefab, toml::table& r_out)
{
    const Guid instance = _guid(r_world, p_root);
    toml::array overrides;
    toml::array removed_parts;
    for (const Entity t : r_prefab.entities) {
        const Guid source = _guid(r_prefab.world, t);
        const bool is_root = source == r_prefab.root;
        const Entity e = is_root ? p_root : r_world.find(r_prefab.target(instance, source));
        const PrefabPartComponent* part = is_root ? nullptr : r_world.try_get<PrefabPartComponent>(e);
        if (!is_root && (!part || part->instance != instance)) {
            removed_parts.push_back(source.to_string());
            continue;
        }
        const PrefabSlot slot{ e, t, instance, is_root };
        toml::table record;
        toml::array tags;
        toml::array removed;
        std::vector<std::string> fields;
        for (const ComponentInfo& info : r_world.components) {
            if (!_saved(info) || (is_root && prefab_owned(info))) continue;
            const bool has = info.has(r_world, e);
            if (info.editor_only) {
                if (has) _write_component(info, r_world, e, record, tags);
                continue;
            }
            if (!_overridden(r_world, slot, r_prefab, info)) continue;
            if (!has) {
                removed.push_back(std::string(info.key));
            } else if (_key_is(info, "parent") || !info.has(r_prefab.world, t)) {
                _write_component(info, r_world, e, record, tags);
            } else {
                _changed_fields(info, r_prefab.world, t, r_world, e, fields);
                _write_component(info, r_world, e, record, tags, &fields);
            }
        }
        if (!tags.empty()) record.insert_or_assign(PrefabKeys::TAGS, std::move(tags));
        if (!removed.empty()) record.insert_or_assign(PrefabKeys::REMOVED, std::move(removed));
        if (record.empty()) continue;
        record.insert_or_assign(PrefabKeys::PART, source.to_string());
        overrides.push_back(std::move(record));
    }
    if (!overrides.empty()) r_out.insert_or_assign(PrefabKeys::OVERRIDES, std::move(overrides));
    if (!removed_parts.empty()) r_out.insert_or_assign(PrefabKeys::REMOVED_PARTS, std::move(removed_parts));
}

void prefab_build(World& r_world, Entity p_root, Prefab& r_prefab, const toml::table* p_capture, bool p_editor)
{
    const Guid instance = _guid(r_world, p_root);
    const Entity troot = r_prefab.root_entity();
    if (!instance.valid() || troot == ENTITY_NULL) return;

    std::vector<Entity> parts;
    _instance_parts(r_world, instance, parts);
    for (const Entity e : parts) r_world.destroy(e);

    for (const ComponentInfo& info : r_world.components) {
        if (_saved(info) && !prefab_owned(info)) info.sync(r_prefab.world, troot, r_world, p_root);
    }

    for (const Entity t : r_prefab.entities) {
        if (t == troot) continue;
        const Guid source = _guid(r_prefab.world, t);
        const Guid guid = r_prefab.target(instance, source);
        if (r_world.find(guid) != ENTITY_NULL) continue;
        const Entity e = r_world.create_persistent(guid);
        for (const ComponentInfo& info : r_world.components) {
            if (!_saved(info) || info.editor_only || _key_is(info, "parent") || _key_is(info, "prefab") || !info.has(r_prefab.world, t)) continue;
            info.sync(r_prefab.world, t, r_world, e);
        }
        const Guid parent = _template_parent(r_prefab, t, instance);
        if (parent.valid()) r_world.add<ParentComponent>(e, { parent });
        r_world.add<PrefabPartComponent>(e, { instance, source });
    }
    if (!p_capture) return;

    if (const toml::array* removed = p_capture->get_as<toml::array>(PrefabKeys::REMOVED_PARTS)) {
        for (const toml::node& n : *removed) {
            const Guid source = Guid::from_string(n.value_or(std::string{}));
            if (!source.valid() || source == r_prefab.root) continue;
            const Entity e = r_world.find(r_prefab.target(instance, source));
            if (r_world.has<PrefabPartComponent>(e)) r_world.destroy(e);
        }
    }
    if (const toml::array* overrides = p_capture->get_as<toml::array>(PrefabKeys::OVERRIDES)) {
        for (const toml::node& n : *overrides) {
            const toml::table* record = n.as_table();
            if (!record) continue;
            const Guid source = Guid::from_string((*record)[PrefabKeys::PART].value_or(std::string{}));
            if (!source.valid()) continue;
            const Entity e = source == r_prefab.root ? p_root : r_world.find(r_prefab.target(instance, source));
            if (e == p_root || r_world.has<PrefabPartComponent>(e)) _apply_record(r_world, e, *record, p_editor);
        }
    }
}

void prefab_refresh(World& r_world, Guid p_prefab, Prefab* p_old, Prefab* p_new, bool p_editor)
{
    std::vector<Entity> roots;
    r_world.view<PrefabInstanceComponent>([&](Entity p_entity, const PrefabInstanceComponent& p_instance) {
        if (p_instance.prefab == p_prefab && !r_world.has<PrefabPartComponent>(p_entity)) roots.push_back(p_entity);
    });
    for (const Entity root : roots) {
        toml::table capture;
        if (p_old) prefab_capture(r_world, root, *p_old, capture);
        if (p_new) prefab_build(r_world, root, *p_new, &capture, p_editor);
    }
}

Entity prefab_spawn(World& r_world, Prefab& r_prefab, Entity p_parent, const TransformComponent& p_transform)
{
    const Entity troot = r_prefab.root_entity();
    if (troot == ENTITY_NULL) return ENTITY_NULL;
    const Entity e = r_world.create_persistent(Guid::generate());
    if (const NameComponent* name = r_prefab.world.try_get<NameComponent>(troot)) r_world.add<NameComponent>(e, *name);
    TransformComponent xf = p_transform;
    if (const TransformComponent* t = r_prefab.world.try_get<TransformComponent>(troot)) {
        xf.rotation = t->rotation * xf.rotation;
        xf.scale = t->scale * xf.scale;
    }
    r_world.add<TransformComponent>(e, xf);
    r_world.add<PrefabInstanceComponent>(e, { r_prefab.guid });
    if (r_world.valid(p_parent)) r_world.add<ParentComponent>(e, { _guid(r_world, p_parent) });
    prefab_build(r_world, e, r_prefab, nullptr, true);
    return e;
}

/*******************/
/**** OVERRIDES ****/
/*******************/

uint64_t prefab_overrides(World& r_world, Entity p_entity, Prefab& r_prefab)
{
    const PrefabSlot slot = _slot(r_world, p_entity, r_prefab);
    if (slot.source == ENTITY_NULL) return 0;
    uint64_t mask = 0;
    for (uint32_t i = 0; i < (uint32_t)r_world.components.size() && i < 64; i++) {
        if (_overridden(r_world, slot, r_prefab, r_world.components[i])) mask |= 1ull << i;
    }
    return mask;
}

bool prefab_instance_overridden(World& r_world, Entity p_root, Prefab& r_prefab)
{
    if (prefab_overrides(r_world, p_root, r_prefab)) return true;
    const Guid instance = _guid(r_world, p_root);
    std::vector<Entity> parts;
    _instance_parts(r_world, instance, parts);
    if (parts.size() + 1 != r_prefab.entities.size()) return true;
    for (const Entity e : parts) {
        if (prefab_overrides(r_world, e, r_prefab)) return true;
    }
    return false;
}

void prefab_overridden_fields(World& r_world, Entity p_entity, Prefab& r_prefab, uint32_t p_component, std::vector<std::string>& r_fields)
{
    r_fields.clear();
    const PrefabSlot slot = _slot(r_world, p_entity, r_prefab);
    if (slot.source == ENTITY_NULL || p_component >= r_world.components.size()) return;
    const ComponentInfo& info = r_world.components[p_component];
    if (!_overridden(r_world, slot, r_prefab, info)) return;
    if (_key_is(info, "parent") || !info.has(r_world, p_entity) || !info.has(r_prefab.world, slot.source)) return;
    _changed_fields(info, r_prefab.world, slot.source, r_world, p_entity, r_fields);
}

void prefab_revert_component(World& r_world, Entity p_entity, Prefab& r_prefab, uint32_t p_component)
{
    const PrefabSlot slot = _slot(r_world, p_entity, r_prefab);
    if (slot.source == ENTITY_NULL || p_component >= r_world.components.size()) return;
    const ComponentInfo& info = r_world.components[p_component];
    if (!_overridden(r_world, slot, r_prefab, info)) return;
    if (_key_is(info, "parent")) _set_parent_guid(r_world, p_entity, _template_parent(r_prefab, slot.source, slot.instance));
    else info.sync(r_prefab.world, slot.source, r_world, p_entity);
}

void prefab_revert_entity(World& r_world, Entity p_entity, Prefab& r_prefab)
{
    for (uint32_t i = 0; i < (uint32_t)r_world.components.size(); i++) prefab_revert_component(r_world, p_entity, r_prefab, i);
}

void prefab_revert_instance(World& r_world, Entity p_root, Prefab& r_prefab, bool p_editor)
{
    toml::table capture;
    prefab_capture(r_world, p_root, r_prefab, capture);
    toml::table editor;
    if (const toml::array* overrides = capture.get_as<toml::array>(PrefabKeys::OVERRIDES)) {
        toml::array kept;
        for (const toml::node& n : *overrides) {
            const toml::table* record = n.as_table();
            if (!record) continue;
            const toml::array* tags = record->get_as<toml::array>(PrefabKeys::TAGS);
            if (!tags) continue;
            toml::array editor_tags;
            for (const ComponentInfo& info : r_world.components) {
                if (info.editor_only && info.key && toml_has_string(tags, info.key)) editor_tags.push_back(std::string(info.key));
            }
            if (editor_tags.empty()) continue;
            toml::table r;
            r.insert_or_assign(PrefabKeys::PART, (*record)[PrefabKeys::PART].value_or(std::string{}));
            r.insert_or_assign(PrefabKeys::TAGS, std::move(editor_tags));
            kept.push_back(std::move(r));
        }
        if (!kept.empty()) editor.insert_or_assign(PrefabKeys::OVERRIDES, std::move(kept));
    }
    prefab_build(r_world, p_root, r_prefab, &editor, p_editor);
}

/*****************/
/**** WRITING ****/
/*****************/

static Error _write_prefab(World& r_world, Guid p_prefab, Guid p_root, const std::filesystem::path& p_path)
{
    WorldFile file;
    file.guid = p_prefab;
    file.root = p_root;
    file.type = AssetType::PREFAB;
    return WorldFile::write(p_path, r_world, file);
}

static void _copy_body(const World& p_src, Entity p_entity, World& r_dst, Entity p_target)
{
    for (const ComponentInfo& info : r_dst.components) {
        if (!_saved(info) || info.editor_only || _key_is(info, "parent") || _key_is(info, "prefab") || !info.has(p_src, p_entity)) continue;
        info.sync(p_src, p_entity, r_dst, p_target);
    }
}

Error prefab_create(World& r_world, const std::vector<Entity>& p_tops, const std::filesystem::path& p_path, Guid& r_prefab, Entity& r_instance)
{
    using enum Error;
    if (p_tops.empty()) return FAILED;
    const bool single = p_tops.size() == 1;
    const Entity first = p_tops.front();
    const Entity parent = r_world.parent(first);

    std::vector<Entity> all;
    std::vector<Entity> sub;
    for (const Entity top : p_tops) {
        r_world.subtree(top, sub);
        all.insert(all.end(), sub.begin(), sub.end());
    }

    vec3 center(0.0f);
    bool centered = !single;
    for (const Entity top : p_tops) {
        centered = centered && r_world.has<TransformComponent>(top);
        center += vec3(r_world.world_matrix(top)[3]);
    }
    center = centered ? center / (float)p_tops.size() : vec3(0.0f);
    const mat4 root_world = translate(mat4(1.0f), center);

    World tmp;
    tmp.initialize();
    std::unordered_map<uint32_t, Guid> mapped;
    const Guid root_guid = Guid::generate();
    for (const Entity e : all) mapped[e.index] = single && e == first ? root_guid : Guid::generate();

    if (!single) {
        const Entity troot = tmp.create_persistent(root_guid);
        NameComponent name{};
        std::snprintf(name.name, NameComponent::MAX_LENGTH, "%s", p_path.stem().string().c_str());
        tmp.add<NameComponent>(troot, name);
        tmp.add<TransformComponent>(troot);
    }
    for (const Entity e : all) {
        const Entity te = tmp.create_persistent(mapped[e.index]);
        _copy_body(r_world, e, tmp, te);
        const bool top = std::find(p_tops.begin(), p_tops.end(), e) != p_tops.end();
        if (!top) {
            tmp.add<ParentComponent>(te, { mapped[r_world.parent(e).index] });
            continue;
        }
        if (single) {
            if (TransformComponent* xf = tmp.try_get<TransformComponent>(te)) xf->position = vec3(0.0f);
            continue;
        }
        tmp.add<ParentComponent>(te, { root_guid });
        if (TransformComponent* xf = tmp.try_get<TransformComponent>(te)) *xf = transform_from_matrix(inverse(root_world) * r_world.world_matrix(e));
    }

    r_prefab = Guid::generate();
    if (_write_prefab(tmp, r_prefab, root_guid, p_path) != OK) return FAILED;
    prefab_library().set_path(r_prefab, p_path);
    Prefab* prefab = prefab_library().get(r_prefab);
    if (!prefab) return FAILED;

    const Guid instance_guid = single ? _guid(r_world, first) : Guid::generate();
    NameComponent name{};
    if (const NameComponent* n = single ? r_world.try_get<NameComponent>(first) : nullptr) name = *n;
    else std::snprintf(name.name, NameComponent::MAX_LENGTH, "%s", p_path.stem().string().c_str());
    TransformComponent xf;
    if (const TransformComponent* t = single ? r_world.try_get<TransformComponent>(first) : nullptr) xf = *t;
    const bool expanded = r_world.has<EditorExpandedTag>(first);
    const Guid parent_guid = _guid(r_world, parent);

    for (size_t i = all.size(); i-- > 0;) {
        if (r_world.valid(all[i])) r_world.destroy(all[i]);
    }

    r_instance = r_world.create_persistent(instance_guid);
    r_world.add<NameComponent>(r_instance, name);
    r_world.add<TransformComponent>(r_instance, xf);
    if (parent_guid.valid()) r_world.add<ParentComponent>(r_instance, { parent_guid });
    r_world.add<PrefabInstanceComponent>(r_instance, { r_prefab });
    if (expanded) r_world.add<EditorExpandedTag>(r_instance);
    if (!single) r_world.set_world_matrix(r_instance, root_world);
    prefab_build(r_world, r_instance, *prefab, nullptr, true);
    return OK;
}

}
