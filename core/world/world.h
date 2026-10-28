#pragma once
#include <core/world/components.h>
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

struct World;

struct ComponentInfo
{
    const char* name;
    bool addable;
    bool (*has)(const World& p_world, Entity p_entity);
    void (*add_default)(World& p_world, Entity p_entity);
};

struct World
{
    /****************/
    /**** SCENE *****/
    /****************/

    Camera default_camera;
    Camera* active_camera = &default_camera;
    AtmosphereComponent atmosphere;

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
    bool valid(Entity p_entity) const { return p_entity.index != Entity::INVALID_INDEX && registry.valid(_to_id(p_entity)); }
    void touch(Entity p_entity) { if (valid(p_entity) && _is_static(p_entity)) static_version++; }

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
        components.push_back({ p_name, p_addable, [](const World& p_world, Entity p_entity) { return p_world.has<T>(p_entity); }, [](World& p_world, Entity p_entity) { p_world.add<T>(p_entity); } });
    }

    template <typename T>
    T* add(Entity p_entity, const T& p_value = {}) {
        LUMEN_ERR_FAIL_COND_V(_iterating != 0, nullptr);
        LUMEN_ERR_FAIL_COND_V(!valid(p_entity), nullptr);
        const EntityId id = _to_id(p_entity);
        structure_version++;
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
