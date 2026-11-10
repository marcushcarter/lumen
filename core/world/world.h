#pragma once
#include <core/world/components.h>
#include <core/world/world_settings.h>
#include <core/world/camera.h>
#include <core/base/error.h>
#include <core/base/id_map.h>
#include <core/assets/guid.h>
#include <entt/entity/registry.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace lumen {

enum class EntityId : uint64_t {};
using Registry = entt::basic_registry<EntityId>;

struct Entity
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;
    uint32_t index;
    uint32_t generation;
    bool operator==(const Entity& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Entity& o) const { return !(*this == o); }
};

static constexpr Entity ENTITY_NULL = { Entity::INVALID_INDEX, 0 };

template <typename... Xs> using Exclude = entt::exclude_t<Xs...>;

TransformComponent transform_from_matrix(const mat4& p_matrix);
mat4 transform_matrix(const TransformComponent& p_transform);

struct World;
struct Archive;

struct ComponentInfo
{
    const char* name;
    bool addable;
    bool (*has)(const World& p_world, Entity p_entity);
    void (*add_default)(World& p_world, Entity p_entity);
    void (*remove)(World& p_world, Entity p_entity);
    const char* key = nullptr;
    bool editor_only = false;
    void (*serialize)(World& r_world, Entity p_entity, Archive& r_archive) = nullptr;
    void (*sync)(const World& p_src, Entity p_src_entity, World& r_dst, Entity p_dst_entity) = nullptr;
};

struct World
{
    /****************/
    /**** SCENE *****/
    /****************/

    Camera default_camera;
    Camera* active_camera = &default_camera;
    EnvironmentSettings environment;
    PlayerSettings player;

    uint64_t static_version = 0;
    uint64_t structure_version = 0;
    uint64_t entity_version = 0;

    Error initialize();
    void shutdown();
    Error load();
    void unload();

    /******************/
    /**** ENTITIES ****/
    /******************/

    using Traits = entt::entt_traits<EntityId>;

    Registry registry;
    IdMap _by_guid;

    static EntityId _to_id(Entity p_entity) { return Traits::construct(p_entity.index, p_entity.generation); }
    static Entity _from_id(EntityId p_id) { return { (uint32_t)Traits::to_entity(p_id), (uint32_t)Traits::to_version(p_id) }; }
    bool _is_static(Entity p_entity) const { return registry.all_of<StaticTag>(_to_id(p_entity)); }
    Entity create();
    Entity create_persistent(Guid p_guid);
    void destroy(Entity p_entity);
    Entity find(Guid p_guid) const;
    Entity entity_at(uint32_t p_index) const;

    /*******************/
    /**** HIERARCHY ****/
    /*******************/

    static constexpr uint32_t MAX_DEPTH = 64;

    Entity parent(Entity p_entity) const;
    bool is_ancestor(Entity p_ancestor, Entity p_entity) const;
    bool set_parent(Entity p_entity, Entity p_parent);
    void children(Entity p_entity, std::vector<Entity>& r_children) const;
    void subtree(Entity p_entity, std::vector<Entity>& r_entities) const;
    void destroy_subtree(Entity p_entity);
    mat4 local_matrix(Entity p_entity) const;
    mat4 world_matrix(Entity p_entity) const;
    mat4 parent_matrix(Entity p_entity) const;
    TransformComponent world_transform(Entity p_entity) const;
    void set_world_matrix(Entity p_entity, const mat4& p_world);
    bool valid(Entity p_entity) const { return p_entity.index != Entity::INVALID_INDEX && registry.valid(_to_id(p_entity)); }
    bool has_children(Entity p_entity) const;
    bool is_folder(Entity p_entity) const;
    void touch(Entity p_entity) {
        if (!valid(p_entity)) return;
        _record(p_entity);
        if (_is_static(p_entity) || has_children(p_entity)) static_version++;
    }

    /*****************/
    /**** CHANGES ****/
    /*****************/

    bool track_changes = false;
    bool changed_all = false;
    std::vector<uint32_t> changed;
    std::vector<uint64_t> destroyed;

    void _record(Entity p_entity) { if (track_changes) changed.push_back(p_entity.index); }
    void changes_clear() { changed.clear(); destroyed.clear(); changed_all = false; }

    /********************/
    /**** COMPONENTS ****/
    /********************/

    std::vector<ComponentInfo> components;

    template <typename T>
    static T& _tag() { static T instance{}; return instance; }

    template <typename T>
    void component_register(const char* p_name, bool p_addable = true) {
        LUMEN_ERR_FAIL_COND(_iterating != 0);
        (void)registry.storage<T>();
        components.push_back({ p_name, p_addable, [](const World& p_world, Entity p_entity) { return p_world.has<T>(p_entity); }, [](World& p_world, Entity p_entity) { p_world.add<T>(p_entity); }, [](World& p_world, Entity p_entity) { p_world.remove<T>(p_entity); } });
    }

    template <typename T>
    void component_register_saved(const char* p_name, const char* p_key, bool p_addable = true, bool p_editor_only = false) {
        component_register<T>(p_name, p_addable);
        ComponentInfo& info = components.back();
        info.key = p_key;
        info.editor_only = p_editor_only;
        info.sync = [](const World& p_src, Entity p_src_entity, World& r_dst, Entity p_dst_entity) {
            if constexpr (std::is_empty_v<T>) {
                if (p_src.has<T>(p_src_entity)) {
                    if (!r_dst.has<T>(p_dst_entity)) r_dst.add<T>(p_dst_entity);
                } else {
                    r_dst.remove<T>(p_dst_entity);
                }
            } else {
                const T* src = p_src.try_get<T>(p_src_entity);
                if (!src) {
                    r_dst.remove<T>(p_dst_entity);
                } else if (T* dst = r_dst.try_get<T>(p_dst_entity)) {
                    *dst = *src;
                    r_dst.touch(p_dst_entity);
                } else {
                    r_dst.add<T>(p_dst_entity, *src);
                }
            }
        };
        info.serialize = [](World& r_world, Entity p_entity, Archive& r_archive) {
            if constexpr (!std::is_empty_v<T>) {
                if (T* c = r_world.try_get<T>(p_entity)) serialize(r_archive, *c);
            } else {
                (void)r_world;
                (void)p_entity;
                (void)r_archive;
            }
        };
    }

    template <typename T>
    T* add(Entity p_entity, const T& p_value = {}) {
        LUMEN_ERR_FAIL_COND_V(_iterating != 0, nullptr);
        LUMEN_ERR_FAIL_COND_V(!valid(p_entity), nullptr);
        const EntityId id = _to_id(p_entity);
        structure_version++;
        _record(p_entity);
        if constexpr (std::is_empty_v<T>) {
            registry.emplace_or_replace<T>(id);
            if (_is_static(p_entity)) static_version++;
            return &_tag<T>();
        } else {
            T& c = registry.emplace_or_replace<T>(id, p_value);
            if (_is_static(p_entity)) static_version++;
            return &c;
        }
    }

    template <typename T>
    void remove(Entity p_entity) {
        LUMEN_ERR_FAIL_COND(_iterating != 0);
        LUMEN_ERR_FAIL_COND(!valid(p_entity));
        if (!has<T>(p_entity)) return;
        if (_is_static(p_entity)) static_version++;
        structure_version++;
        _record(p_entity);
        registry.remove<T>(_to_id(p_entity));
    }

    template <typename T>
    bool has(Entity p_entity) const { return valid(p_entity) && registry.all_of<T>(_to_id(p_entity)); }

    template <typename T>
    T* try_get(Entity p_entity) {
        if (!has<T>(p_entity)) return nullptr;
        if constexpr (std::is_empty_v<T>) return &_tag<T>();
        else return &registry.get<T>(_to_id(p_entity));
    }

    template <typename T>
    const T* try_get(Entity p_entity) const {
        if (!has<T>(p_entity)) return nullptr;
        if constexpr (std::is_empty_v<T>) return &_tag<T>();
        else return &registry.get<T>(_to_id(p_entity));
    }

    /***************/
    /**** VIEWS ****/
    /***************/

    mutable uint32_t _iterating = 0;

    template <typename T, typename V>
    static decltype(auto) _get(V& p_view, EntityId p_id) {
        if constexpr (std::is_empty_v<T>) return (_tag<T>());
        else return p_view.template get<T>(p_id);
    }

    template <typename... Ts, typename... Xs, typename W, typename Fn>
    static void _view(W& p_world, Exclude<Xs...>, Fn& p_fn) {
        static_assert(sizeof...(Ts) > 0);
        p_world._iterating++;
        auto v = p_world.registry.template view<Ts...>(entt::exclude<Xs...>);
        for (const EntityId id : v) p_fn(_from_id(id), _get<Ts>(v, id)...);
        p_world._iterating--;
    }

    template <typename... Ts, typename Fn>
    void view(Fn&& p_fn) { _view<Ts...>(*this, Exclude<>{}, p_fn); }

    template <typename... Ts, typename Fn>
    void view(Fn&& p_fn) const { _view<Ts...>(*this, Exclude<>{}, p_fn); }

    template <typename... Ts, typename... Xs, typename Fn>
    void view(Exclude<Xs...> p_exclude, Fn&& p_fn) { _view<Ts...>(*this, p_exclude, p_fn); }

    template <typename... Ts, typename... Xs, typename Fn>
    void view(Exclude<Xs...> p_exclude, Fn&& p_fn) const { _view<Ts...>(*this, p_exclude, p_fn); }

    /******************/
    /**** DEFERRED ****/
    /******************/

    std::vector<std::function<void(World&)>> _deferred;

    void deferred_destroy(Entity p_entity) {
        _deferred.push_back([p_entity](World& p_world) { if (p_world.valid(p_entity)) p_world.destroy(p_entity); });
    }

    template <typename T>
    void deferred_add(Entity p_entity, const T& p_value = {}) {
        _deferred.push_back([p_entity, p_value](World& p_world) { if (p_world.valid(p_entity)) p_world.add<T>(p_entity, p_value); });
    }

    template <typename T>
    void deferred_remove(Entity p_entity) {
        _deferred.push_back([p_entity](World& p_world) { if (p_world.valid(p_entity)) p_world.remove<T>(p_entity); });
    }

    void deferred_add_component(Entity p_entity, uint32_t p_component) {
        LUMEN_ERR_FAIL_COND(p_component >= components.size());
        _deferred.push_back([p_entity, p_component](World& p_world) { if (p_world.valid(p_entity)) p_world.components[p_component].add_default(p_world, p_entity); });
    }

    void deferred_flush();
};

}
