#include <core/scene/template.h>
#include <core/scene/scene_file.h>
#include <core/scene/toml_archive.h>
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

TemplateLibrary& template_library()
{
    static TemplateLibrary library;
    return library;
}

void TemplateLibrary::index(const std::filesystem::path& p_dir)
{
    std::error_code ec;
    if (p_dir.empty() || !std::filesystem::is_directory(p_dir, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(p_dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != EXTENSION) continue;
        const AssetInfo info = read_asset_info(it->path());
        if (info.valid() && info.type == AssetType::TEMPLATE) paths[info.guid] = it->path();
    }
}

void TemplateLibrary::set_path(Guid p_template, const std::filesystem::path& p_path)
{
    paths[p_template] = p_path;
}

std::filesystem::path TemplateLibrary::path(Guid p_template) const
{
    if (resolve) {
        std::filesystem::path found = resolve(p_template);
        if (!found.empty()) return found;
    }
    auto it = paths.find(p_template);
    return it == paths.end() ? std::filesystem::path{} : it->second;
}

static void _finish(Template& r_template, Guid p_root)
{
    Scene& world = r_template.world;
    std::vector<Entity> roots;
    world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
        if (world.parent(p_entity) == ENTITY_NULL) roots.push_back(p_entity);
    });
    std::sort(roots.begin(), roots.end(), [](Entity a, Entity b) { return a.index < b.index; });
    if (roots.empty()) return;
    Entity root = roots.front();
    const Entity preferred = world.find(p_root);
    if (preferred != ENTITY_NULL && world.parent(preferred) == ENTITY_NULL) root = preferred;
    r_template.root = world.try_get<EntityIdComponent>(root)->guid;
    for (const Entity e : roots) {
        if (e != root) world.add<ParentComponent>(e, { r_template.root });
    }
    world.subtree(root, r_template.entities);
}

Template* TemplateLibrary::get(Guid p_template)
{
    if (!p_template.valid()) return nullptr;
    if (auto it = cache.find(p_template); it != cache.end()) return it->second.get();
    if (std::find(_loading.begin(), _loading.end(), p_template) != _loading.end()) {
        log_write("Template: %s contains itself, skipping the nested copy.", p_template.to_string().c_str());
        return nullptr;
    }
    const std::filesystem::path file = path(p_template);
    if (file.empty()) {
        log_write("Template: no template with guid %s.", p_template.to_string().c_str());
        cache.emplace(p_template, nullptr);
        return nullptr;
    }

    auto tmpl = std::make_unique<Template>();
    tmpl->guid = p_template;
    tmpl->path = file;
    tmpl->world.initialize();
    _loading.push_back(p_template);
    SceneFile wf;
    const Error err = SceneFile::read(file, tmpl->world, false, wf);
    _loading.pop_back();
    if (err != Error::OK || wf.type != AssetType::TEMPLATE) {
        log_write("Template: failed to load '%s'.", file.string().c_str());
        cache.emplace(p_template, nullptr);
        return nullptr;
    }
    _finish(*tmpl, wf.root);
    Template* out = tmpl.get();
    cache.emplace(p_template, std::move(tmpl));
    version++;
    return out;
}

std::unique_ptr<Template> TemplateLibrary::take(Guid p_template)
{
    auto it = cache.find(p_template);
    if (it == cache.end()) return nullptr;
    std::unique_ptr<Template> out = std::move(it->second);
    cache.erase(it);
    version++;
    return out;
}

void TemplateLibrary::clear()
{
    cache.clear();
    paths.clear();
    version++;
}

/*****************/
/**** HELPERS ****/
/*****************/

Guid template_part_guid(Guid p_instance, Guid p_source)
{
    Guid g{ hash_splitmix(hash_combine(hash_splitmix(p_instance.value), p_source.value)) };
    if (!g.valid()) g.value = 1;
    return g;
}

Guid Template::target(Guid p_instance, Guid p_source) const
{
    return p_source == root ? p_instance : template_part_guid(p_instance, p_source);
}

static bool _key_is(const ComponentInfo& p_info, const char* p_key)
{
    return p_info.key && std::strcmp(p_info.key, p_key) == 0;
}

static bool _saved(const ComponentInfo& p_info)
{
    return p_info.key && p_info.sync && p_info.serialize;
}

bool template_owned(const ComponentInfo& p_info)
{
    return p_info.editor_only || _key_is(p_info, "transform") || _key_is(p_info, "parent") || _key_is(p_info, "name") || _key_is(p_info, "template");
}

static Guid _guid(const Scene& p_world, Entity p_entity)
{
    const EntityIdComponent* id = p_world.try_get<EntityIdComponent>(p_entity);
    return id ? id->guid : Guid{};
}

Entity template_root_of(const Scene& p_world, Entity p_entity)
{
    if (const TemplatePartComponent* part = p_world.try_get<TemplatePartComponent>(p_entity)) return p_world.find(part->instance);
    if (p_world.has<TemplateInstanceComponent>(p_entity)) return p_entity;
    return ENTITY_NULL;
}

bool template_is_part(const Scene& p_world, Entity p_entity)
{
    return p_world.has<TemplatePartComponent>(p_entity);
}

Template* template_for(const Scene& p_world, Entity p_entity)
{
    const Entity root = template_root_of(p_world, p_entity);
    const TemplateInstanceComponent* instance = p_world.try_get<TemplateInstanceComponent>(root);
    return instance ? template_library().get(instance->tmpl) : nullptr;
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

static void _changed_fields(const ComponentInfo& p_info, Scene& r_base, Entity p_base, Scene& r_world, Entity p_entity, std::vector<std::string>& r_fields)
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

static bool _equal(const ComponentInfo& p_info, Scene& r_a, Entity p_a, Scene& r_b, Entity p_b)
{
    const bool has_a = p_info.has(r_a, p_a);
    const bool has_b = p_info.has(r_b, p_b);
    if (has_a != has_b) return false;
    if (!has_a) return true;
    std::vector<std::string> fields;
    _changed_fields(p_info, r_a, p_a, r_b, p_b, fields);
    return fields.empty();
}

static Guid _template_parent(const Template& p_source, Entity p_template, Guid p_instance)
{
    const ParentComponent* parent = p_source.world.try_get<ParentComponent>(p_template);
    return parent && parent->parent.valid() ? p_source.target(p_instance, parent->parent) : Guid{};
}

static Guid _parent_guid(const Scene& p_world, Entity p_entity)
{
    const ParentComponent* parent = p_world.try_get<ParentComponent>(p_entity);
    return parent ? parent->parent : Guid{};
}

static void _set_parent_guid(Scene& r_world, Entity p_entity, Guid p_parent)
{
    if (_parent_guid(r_world, p_entity) == p_parent) return;
    if (p_parent.valid()) r_world.add<ParentComponent>(p_entity, { p_parent });
    else r_world.remove<ParentComponent>(p_entity);
    r_world.touch(p_entity);
}

struct TemplateSlot
{
    Entity entity = ENTITY_NULL;
    Entity source = ENTITY_NULL;
    Guid instance;
    bool root = false;
};

static TemplateSlot _slot(Scene& r_world, Entity p_entity, Template& r_template)
{
    TemplateSlot slot;
    slot.entity = p_entity;
    if (const TemplatePartComponent* part = r_world.try_get<TemplatePartComponent>(p_entity)) {
        slot.instance = part->instance;
        slot.source = r_template.world.find(part->source);
    } else if (r_world.has<TemplateInstanceComponent>(p_entity)) {
        slot.instance = _guid(r_world, p_entity);
        slot.source = r_template.root_entity();
        slot.root = true;
    }
    return slot;
}

static bool _overridden(Scene& r_world, const TemplateSlot& p_slot, Template& r_template, const ComponentInfo& p_info)
{
    if (!_saved(p_info) || p_info.editor_only || (p_slot.root && template_owned(p_info))) return false;
    if (_key_is(p_info, "parent")) return _parent_guid(r_world, p_slot.entity) != _template_parent(r_template, p_slot.source, p_slot.instance);
    return !_equal(p_info, r_template.world, p_slot.source, r_world, p_slot.entity);
}

static void _write_component(const ComponentInfo& p_info, Scene& r_world, Entity p_entity, toml::table& r_record, toml::array& r_tags, const std::vector<std::string>* p_only = nullptr)
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

static void _apply_record(Scene& r_world, Entity p_entity, const toml::table& p_record, bool p_editor)
{
    const toml::array* tags = p_record.get_as<toml::array>(TemplateKeys::TAGS);
    const toml::array* removed = p_record.get_as<toml::array>(TemplateKeys::REMOVED);
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

static void _instance_parts(const Scene& p_world, Guid p_instance, std::vector<Entity>& r_parts)
{
    r_parts.clear();
    p_world.view<TemplatePartComponent>([&](Entity p_entity, const TemplatePartComponent& p_part) {
        if (p_part.instance == p_instance) r_parts.push_back(p_entity);
    });
}

/*******************/
/**** INSTANCES ****/
/*******************/

void template_capture(Scene& r_world, Entity p_root, Template& r_template, toml::table& r_out)
{
    const Guid instance = _guid(r_world, p_root);
    toml::array overrides;
    toml::array removed_parts;
    for (const Entity t : r_template.entities) {
        const Guid source = _guid(r_template.world, t);
        const bool is_root = source == r_template.root;
        const Entity e = is_root ? p_root : r_world.find(r_template.target(instance, source));
        const TemplatePartComponent* part = is_root ? nullptr : r_world.try_get<TemplatePartComponent>(e);
        if (!is_root && (!part || part->instance != instance)) {
            removed_parts.push_back(source.to_string());
            continue;
        }
        const TemplateSlot slot{ e, t, instance, is_root };
        toml::table record;
        toml::array tags;
        toml::array removed;
        std::vector<std::string> fields;
        for (const ComponentInfo& info : r_world.components) {
            if (!_saved(info) || (is_root && template_owned(info))) continue;
            const bool has = info.has(r_world, e);
            if (info.editor_only) {
                if (has) _write_component(info, r_world, e, record, tags);
                continue;
            }
            if (!_overridden(r_world, slot, r_template, info)) continue;
            if (!has) {
                removed.push_back(std::string(info.key));
            } else if (_key_is(info, "parent") || !info.has(r_template.world, t)) {
                _write_component(info, r_world, e, record, tags);
            } else {
                _changed_fields(info, r_template.world, t, r_world, e, fields);
                _write_component(info, r_world, e, record, tags, &fields);
            }
        }
        if (!tags.empty()) record.insert_or_assign(TemplateKeys::TAGS, std::move(tags));
        if (!removed.empty()) record.insert_or_assign(TemplateKeys::REMOVED, std::move(removed));
        if (record.empty()) continue;
        record.insert_or_assign(TemplateKeys::PART, source.to_string());
        overrides.push_back(std::move(record));
    }
    if (!overrides.empty()) r_out.insert_or_assign(TemplateKeys::OVERRIDES, std::move(overrides));
    if (!removed_parts.empty()) r_out.insert_or_assign(TemplateKeys::REMOVED_PARTS, std::move(removed_parts));
}

void template_build(Scene& r_world, Entity p_root, Template& r_template, const toml::table* p_capture, bool p_editor)
{
    const Guid instance = _guid(r_world, p_root);
    const Entity troot = r_template.root_entity();
    if (!instance.valid() || troot == ENTITY_NULL) return;

    std::vector<Entity> parts;
    _instance_parts(r_world, instance, parts);
    for (const Entity e : parts) r_world.destroy(e);

    for (const ComponentInfo& info : r_world.components) {
        if (_saved(info) && !template_owned(info)) info.sync(r_template.world, troot, r_world, p_root);
    }

    for (const Entity t : r_template.entities) {
        if (t == troot) continue;
        const Guid source = _guid(r_template.world, t);
        const Guid guid = r_template.target(instance, source);
        if (r_world.find(guid) != ENTITY_NULL) continue;
        const Entity e = r_world.create_persistent(guid);
        for (const ComponentInfo& info : r_world.components) {
            if (!_saved(info) || info.editor_only || _key_is(info, "parent") || _key_is(info, "template") || !info.has(r_template.world, t)) continue;
            info.sync(r_template.world, t, r_world, e);
        }
        const Guid parent = _template_parent(r_template, t, instance);
        if (parent.valid()) r_world.add<ParentComponent>(e, { parent });
        r_world.add<TemplatePartComponent>(e, { instance, source });
    }
    if (!p_capture) return;

    if (const toml::array* removed = p_capture->get_as<toml::array>(TemplateKeys::REMOVED_PARTS)) {
        for (const toml::node& n : *removed) {
            const Guid source = Guid::from_string(n.value_or(std::string{}));
            if (!source.valid() || source == r_template.root) continue;
            const Entity e = r_world.find(r_template.target(instance, source));
            if (r_world.has<TemplatePartComponent>(e)) r_world.destroy(e);
        }
    }
    if (const toml::array* overrides = p_capture->get_as<toml::array>(TemplateKeys::OVERRIDES)) {
        for (const toml::node& n : *overrides) {
            const toml::table* record = n.as_table();
            if (!record) continue;
            const Guid source = Guid::from_string((*record)[TemplateKeys::PART].value_or(std::string{}));
            if (!source.valid()) continue;
            const Entity e = source == r_template.root ? p_root : r_world.find(r_template.target(instance, source));
            if (e == p_root || r_world.has<TemplatePartComponent>(e)) _apply_record(r_world, e, *record, p_editor);
        }
    }
}

void template_refresh(Scene& r_world, Guid p_template, Template* p_old, Template* p_new, bool p_editor)
{
    std::vector<Entity> roots;
    r_world.view<TemplateInstanceComponent>([&](Entity p_entity, const TemplateInstanceComponent& p_instance) {
        if (p_instance.tmpl == p_template && !r_world.has<TemplatePartComponent>(p_entity)) roots.push_back(p_entity);
    });
    for (const Entity root : roots) {
        toml::table capture;
        if (p_old) template_capture(r_world, root, *p_old, capture);
        if (p_new) template_build(r_world, root, *p_new, &capture, p_editor);
    }
}

Entity template_spawn(Scene& r_world, Template& r_template, Entity p_parent, const TransformComponent& p_transform)
{
    const Entity troot = r_template.root_entity();
    if (troot == ENTITY_NULL) return ENTITY_NULL;
    const Entity e = r_world.create_persistent(Guid::generate());
    if (const NameComponent* name = r_template.world.try_get<NameComponent>(troot)) r_world.add<NameComponent>(e, *name);
    TransformComponent xf = p_transform;
    if (const TransformComponent* t = r_template.world.try_get<TransformComponent>(troot)) {
        xf.rotation = t->rotation * xf.rotation;
        xf.scale = t->scale * xf.scale;
    }
    r_world.add<TransformComponent>(e, xf);
    r_world.add<TemplateInstanceComponent>(e, { r_template.guid });
    if (r_world.valid(p_parent)) r_world.add<ParentComponent>(e, { _guid(r_world, p_parent) });
    template_build(r_world, e, r_template, nullptr, true);
    return e;
}

/*******************/
/**** OVERRIDES ****/
/*******************/

uint64_t template_overrides(Scene& r_world, Entity p_entity, Template& r_template)
{
    const TemplateSlot slot = _slot(r_world, p_entity, r_template);
    if (slot.source == ENTITY_NULL) return 0;
    uint64_t mask = 0;
    for (uint32_t i = 0; i < (uint32_t)r_world.components.size() && i < 64; i++) {
        if (_overridden(r_world, slot, r_template, r_world.components[i])) mask |= 1ull << i;
    }
    return mask;
}

bool template_instance_overridden(Scene& r_world, Entity p_root, Template& r_template)
{
    if (template_overrides(r_world, p_root, r_template)) return true;
    const Guid instance = _guid(r_world, p_root);
    std::vector<Entity> parts;
    _instance_parts(r_world, instance, parts);
    if (parts.size() + 1 != r_template.entities.size()) return true;
    for (const Entity e : parts) {
        if (template_overrides(r_world, e, r_template)) return true;
    }
    return false;
}

void template_overridden_fields(Scene& r_world, Entity p_entity, Template& r_template, uint32_t p_component, std::vector<std::string>& r_fields)
{
    r_fields.clear();
    const TemplateSlot slot = _slot(r_world, p_entity, r_template);
    if (slot.source == ENTITY_NULL || p_component >= r_world.components.size()) return;
    const ComponentInfo& info = r_world.components[p_component];
    if (!_overridden(r_world, slot, r_template, info)) return;
    if (_key_is(info, "parent") || !info.has(r_world, p_entity) || !info.has(r_template.world, slot.source)) return;
    _changed_fields(info, r_template.world, slot.source, r_world, p_entity, r_fields);
}

void template_revert_component(Scene& r_world, Entity p_entity, Template& r_template, uint32_t p_component)
{
    const TemplateSlot slot = _slot(r_world, p_entity, r_template);
    if (slot.source == ENTITY_NULL || p_component >= r_world.components.size()) return;
    const ComponentInfo& info = r_world.components[p_component];
    if (!_overridden(r_world, slot, r_template, info)) return;
    if (_key_is(info, "parent")) _set_parent_guid(r_world, p_entity, _template_parent(r_template, slot.source, slot.instance));
    else info.sync(r_template.world, slot.source, r_world, p_entity);
}

void template_revert_entity(Scene& r_world, Entity p_entity, Template& r_template)
{
    for (uint32_t i = 0; i < (uint32_t)r_world.components.size(); i++) template_revert_component(r_world, p_entity, r_template, i);
}

void template_revert_instance(Scene& r_world, Entity p_root, Template& r_template, bool p_editor)
{
    toml::table capture;
    template_capture(r_world, p_root, r_template, capture);
    toml::table editor;
    if (const toml::array* overrides = capture.get_as<toml::array>(TemplateKeys::OVERRIDES)) {
        toml::array kept;
        for (const toml::node& n : *overrides) {
            const toml::table* record = n.as_table();
            if (!record) continue;
            const toml::array* tags = record->get_as<toml::array>(TemplateKeys::TAGS);
            if (!tags) continue;
            toml::array editor_tags;
            for (const ComponentInfo& info : r_world.components) {
                if (info.editor_only && info.key && toml_has_string(tags, info.key)) editor_tags.push_back(std::string(info.key));
            }
            if (editor_tags.empty()) continue;
            toml::table r;
            r.insert_or_assign(TemplateKeys::PART, (*record)[TemplateKeys::PART].value_or(std::string{}));
            r.insert_or_assign(TemplateKeys::TAGS, std::move(editor_tags));
            kept.push_back(std::move(r));
        }
        if (!kept.empty()) editor.insert_or_assign(TemplateKeys::OVERRIDES, std::move(kept));
    }
    template_build(r_world, p_root, r_template, &editor, p_editor);
}

/*****************/
/**** WRITING ****/
/*****************/

static Error _write_template(Scene& r_world, Guid p_template, Guid p_root, const std::filesystem::path& p_path)
{
    SceneFile file;
    file.guid = p_template;
    file.root = p_root;
    file.type = AssetType::TEMPLATE;
    return SceneFile::write(p_path, r_world, file);
}

static void _copy_body(const Scene& p_src, Entity p_entity, Scene& r_dst, Entity p_target)
{
    for (const ComponentInfo& info : r_dst.components) {
        if (!_saved(info) || info.editor_only || _key_is(info, "parent") || _key_is(info, "template") || !info.has(p_src, p_entity)) continue;
        info.sync(p_src, p_entity, r_dst, p_target);
    }
}

Error template_create(Scene& r_world, const std::vector<Entity>& p_tops, const std::filesystem::path& p_path, Guid& r_template, Entity& r_instance)
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

    Scene tmp;
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

    r_template = Guid::generate();
    if (_write_template(tmp, r_template, root_guid, p_path) != OK) return FAILED;
    template_library().set_path(r_template, p_path);
    Template* tmpl = template_library().get(r_template);
    if (!tmpl) return FAILED;

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
    r_world.add<TemplateInstanceComponent>(r_instance, { r_template });
    if (expanded) r_world.add<EditorExpandedTag>(r_instance);
    if (!single) r_world.set_world_matrix(r_instance, root_world);
    template_build(r_world, r_instance, *tmpl, nullptr, true);
    return OK;
}

}
