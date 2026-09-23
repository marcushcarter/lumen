#pragma once
#include <core/world/camera.h>
#include <core/base/error.h>
#include <cstdint>
#include <vector>
#include <memory>
#include <tuple>

namespace lumen {

struct Entity
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;
    uint32_t index;
    uint32_t generation;
    bool operator==(const Entity& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Entity& o) const { return !(*this == o); }
};

static constexpr Entity ENTITY_NULL = { Entity::INVALID_INDEX, 0 };

inline uint32_t _next_component_type_id() { static uint32_t id = 0; return id++; }
template <typename T>
uint32_t component_type_id() { static const uint32_t id = _next_component_type_id(); return id; }

struct IComponentPool
{
    virtual ~IComponentPool() = default;
    virtual void _remove(Entity e) = 0;
};

template <typename T>
struct ComponentPool : IComponentPool
{
    static constexpr uint32_t INVALID = 0xFFFFFFFF;
    std::vector<uint32_t> sparse;
    std::vector<Entity> dense;
    std::vector<T> components;

    T& add(Entity e, const T& value) {
        if (e.index >= sparse.size()) sparse.resize(e.index + 1, INVALID);
        uint32_t pos = sparse[e.index];
        if (pos != INVALID) { components[pos] = value; dense[pos] = e; return components[pos]; }
        sparse[e.index] = (uint32_t)dense.size();
        dense.push_back(e);
        components.push_back(value);
        return components.back();
    }

    bool has(Entity e) const {
        return e.index < sparse.size() && sparse[e.index] != INVALID && dense[sparse[e.index]].generation == e.generation;
    }

    T* try_get(Entity e) {
        if (!has(e)) return nullptr;
        return &components[sparse[e.index]];
    }

    void _remove(Entity e) override {
        if (!has(e)) return;
        uint32_t pos = sparse[e.index];
        uint32_t last = (uint32_t)dense.size() - 1;
        Entity last_e = dense[last];
        dense[pos] = last_e;
        components[pos] = std::move(components[last]);
        sparse[last_e.index] = pos;
        dense.pop_back();
        components.pop_back();
        sparse[e.index] = INVALID;
    }
};

struct World
{
    Camera default_camera;
    Camera* active_camera = &default_camera;
    
    std::vector<uint32_t> generations;
    std::vector<uint32_t> free_list;
    std::vector<std::unique_ptr<IComponentPool>> pools;
    
    Error initialize();
    void shutdown();

    Error load();
    void unload();
    
    Entity create();
    void destroy(Entity e);

    bool valid(Entity e) const {
        return e.index < generations.size() && generations[e.index] == e.generation;
    }

    template <typename T>
    ComponentPool<T>& _pool() {
        uint32_t id = component_type_id<T>();
        if (id >= pools.size()) pools.resize(id + 1);
        if (!pools[id]) pools[id] = std::make_unique<ComponentPool<T>>();
        return *static_cast<ComponentPool<T>*>(pools[id].get());
    }

    template <typename T>
    ComponentPool<T>* _pool_ptr() {
        uint32_t id = component_type_id<T>();
        if (id >= pools.size() || !pools[id]) return nullptr;
        return static_cast<ComponentPool<T>*>(pools[id].get());
    }

    template <typename T>
    T& add(Entity e, const T& value = {}) { return _pool<T>().add(e, value); }

    template <typename T>
    bool has(Entity e) { auto* p = _pool_ptr<T>(); return p && p->has(e); }

    template <typename T>
    T* try_get(Entity e) { auto* p = _pool_ptr<T>(); return p ? p->try_get(e) : nullptr; }

    template <typename T>
    T& get(Entity e) { return *try_get<T>(e); }

    template <typename T>
    void remove(Entity e) { auto* p = _pool_ptr<T>(); if (p) p->_remove(e); }

    template <typename... Ts, typename Fn>
    void view(Fn&& fn) {
        using First = std::tuple_element_t<0, std::tuple<Ts...>>;
        auto* p = _pool_ptr<First>();
        if (!p) return;
        for (size_t i = 0; i < p->dense.size(); ++i) {
            Entity e = p->dense[i];
            if ((has<Ts>(e) && ...)) { fn(e, get<Ts>(e)...); }
        }
    }
};

}