#pragma once
#include <core/world/components.h>
#include <core/world/camera.h>
#include <core/base/error.h>
#include <core/assets/guid.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

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

static constexpr uint32_t MAX_COMPONENT_TYPES = 128;

inline uint32_t _component_type_count = 0;

template <typename T> struct ComponentType
{
    static constexpr uint32_t INVALID = 0xFFFFFFFF;
    static inline uint32_t id = INVALID;
};

struct ComponentMask
{
    static constexpr uint32_t WORDS = MAX_COMPONENT_TYPES / 64;
    uint64_t words[WORDS] = {};
    void set(uint32_t p_id) { words[p_id >> 6] |= 1ull << (p_id & 63); }
    void clear(uint32_t p_id) { words[p_id >> 6] &= ~(1ull << (p_id & 63)); }
    bool test(uint32_t p_id) const { return (words[p_id >> 6] >> (p_id & 63)) & 1ull; }
    void reset() { for (uint64_t& w : words) w = 0; }
    bool contains(const ComponentMask& p_other) const {
        for (uint32_t w = 0; w < WORDS; w++) {
            if ((words[w] & p_other.words[w]) != p_other.words[w]) return false;
        }
        return true;
    }
};

struct SparseSet
{
    static constexpr uint32_t PAGE_BITS = 12;
    static constexpr uint32_t PAGE_SIZE = 1u << PAGE_BITS;
    static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;
    static constexpr uint32_t INVALID = 0xFFFFFFFF;

    std::vector<std::unique_ptr<uint32_t[]>> pages;
    std::vector<Entity> dense;

    uint32_t size() const { return (uint32_t)dense.size(); }

    uint32_t find(uint32_t p_index) const {
        const uint32_t page = p_index >> PAGE_BITS;
        if (page >= pages.size() || !pages[page]) return INVALID;
        return pages[page][p_index & PAGE_MASK];
    }

    uint32_t _insert(Entity p_entity) {
        const uint32_t page = p_entity.index >> PAGE_BITS;
        if (page >= pages.size()) pages.resize(page + 1);
        if (!pages[page]) {
            pages[page] = std::make_unique_for_overwrite<uint32_t[]>(PAGE_SIZE);
            std::fill_n(pages[page].get(), PAGE_SIZE, INVALID);
        }
        const uint32_t pos = (uint32_t)dense.size();
        pages[page][p_entity.index & PAGE_MASK] = pos;
        dense.push_back(p_entity);
        return pos;
    }

    uint32_t _erase(uint32_t p_index) {
        const uint32_t pos = find(p_index);
        const Entity moved = dense.back();
        dense[pos] = moved;
        pages[moved.index >> PAGE_BITS][moved.index & PAGE_MASK] = pos;
        pages[p_index >> PAGE_BITS][p_index & PAGE_MASK] = INVALID;
        dense.pop_back();
        return pos;
    }

    void _set_clear() {
        pages.clear();
        dense.clear();
    }
};

template <typename T> struct ComponentPool : SparseSet
{
    static constexpr bool IS_TAG = std::is_empty_v<T>;

    static inline T tag_instance{};

    std::vector<T> components;

    T* _at(uint32_t p_pos) {
        if constexpr (IS_TAG) return &tag_instance;
        else return &components[p_pos];
    }

    const T* _at(uint32_t p_pos) const {
        if constexpr (IS_TAG) return &tag_instance;
        else return &components[p_pos];
    }

    T* _add(Entity p_entity, const T& p_value) {
        uint32_t pos = find(p_entity.index);
        if (pos == INVALID) {
            pos = _insert(p_entity);
            if constexpr (!IS_TAG) components.push_back(p_value);
        } else if constexpr (!IS_TAG) {
            components[pos] = p_value;
        }
        return _at(pos);
    }

    void _remove(uint32_t p_index) {
        const uint32_t pos = _erase(p_index);
        if constexpr (!IS_TAG) {
            if (pos != components.size() - 1) components[pos] = components.back();
            components.pop_back();
        }
    }

    void _pool_clear() {
        _set_clear();
        if constexpr (!IS_TAG) components.clear();
    }
};

struct PoolSlot
{
    SparseSet* set = nullptr;
    void (*remove_fn)(SparseSet* p_set, uint32_t p_index) = nullptr;
    void (*clear_fn)(SparseSet* p_set) = nullptr;
    void (*free_fn)(SparseSet* p_set) = nullptr;
    void (*add_bytes_fn)(SparseSet* p_set, Entity p_entity, const std::byte* p_value) = nullptr;
};

struct WorldCommandBuffer
{
    enum class Op : uint32_t { DESTROY, ADD, REMOVE, };

    struct Command {
        Op op;
        uint32_t type;
        Entity entity;
        uint32_t value_offset;
    };

    std::vector<Command> commands;
    std::vector<std::byte> values;

    void clear() {
        commands.clear();
        values.clear();
    }
};

struct World
{
    Camera default_camera;
    Camera* active_camera = &default_camera;

    std::vector<uint32_t> generations;
    std::vector<ComponentMask> masks;
    std::vector<uint32_t> free_list;

    std::vector<PoolSlot> pools;

    mutable uint32_t iterating = 0;
    WorldCommandBuffer deferred;

    std::unordered_map<Guid, Entity> by_guid;

    Error initialize();
    void shutdown();

    Error load();
    void unload();

    const Camera& camera_active() const { return default_camera; }

    Entity create();
    Entity create_persistent(Guid p_guid);
    void destroy(Entity p_entity);
    Entity find(Guid p_guid) const;

    bool valid(Entity p_entity) const {
        return p_entity.index < generations.size() && generations[p_entity.index] == p_entity.generation;
    }

    template <typename T>
    void component_register() {
        static_assert(std::is_trivially_copyable_v<T>, "components must be trivially copyable");
        LUMEN_ERR_FAIL_COND(iterating != 0);
        uint32_t& id = ComponentType<T>::id;
        if (id == ComponentType<T>::INVALID) {
            LUMEN_ERR_FAIL_COND(_component_type_count >= MAX_COMPONENT_TYPES);
            id = _component_type_count++;
        }
        if (id >= pools.size()) pools.resize(id + 1);
        PoolSlot& slot = pools[id];
        if (slot.set) return;
        slot.set = new ComponentPool<T>();
        slot.remove_fn = [](SparseSet* p_set, uint32_t p_index) { static_cast<ComponentPool<T>*>(p_set)->_remove(p_index); };
        slot.clear_fn = [](SparseSet* p_set) { static_cast<ComponentPool<T>*>(p_set)->_pool_clear(); };
        slot.free_fn = [](SparseSet* p_set) { delete static_cast<ComponentPool<T>*>(p_set); };
        slot.add_bytes_fn = [](SparseSet* p_set, Entity p_entity, const std::byte* p_value) {
            T value;
            std::memcpy(&value, p_value, sizeof(T));
            static_cast<ComponentPool<T>*>(p_set)->_add(p_entity, value);
        };
    }

    template <typename T>
    T* add(Entity p_entity, const T& p_value = {}) {
        LUMEN_ERR_FAIL_COND_V(iterating != 0, nullptr);
        LUMEN_ERR_FAIL_COND_V(!valid(p_entity), nullptr);
        LUMEN_ERR_FAIL_COND_V(!_registered<T>(), nullptr);
        masks[p_entity.index].set(ComponentType<T>::id);
        return _pool<T>(*this)->_add(p_entity, p_value);
    }

    template <typename T>
    void remove(Entity p_entity) {
        LUMEN_ERR_FAIL_COND(iterating != 0);
        LUMEN_ERR_FAIL_COND(!valid(p_entity));
        LUMEN_ERR_FAIL_COND(!_registered<T>());
        _remove_id(p_entity, ComponentType<T>::id);
    }

    template <typename T>
    bool has(Entity p_entity) const {
        const uint32_t id = ComponentType<T>::id;
        return id != ComponentType<T>::INVALID && valid(p_entity) && masks[p_entity.index].test(id);
    }

    template <typename T>
    T* try_get(Entity p_entity) {
        if (!has<T>(p_entity)) return nullptr;
        ComponentPool<T>* pool = _pool<T>(*this);
        return pool->_at(pool->find(p_entity.index));
    }

    template <typename T>
    const T* try_get(Entity p_entity) const {
        if (!has<T>(p_entity)) return nullptr;
        const ComponentPool<T>* pool = _pool<T>(*this);
        return pool->_at(pool->find(p_entity.index));
    }

    template <typename... Ts, typename Fn>
    void view(Fn&& p_fn) { _view<Ts...>(*this, p_fn); }

    template <typename... Ts, typename Fn>
    void view(Fn&& p_fn) const { _view<Ts...>(*this, p_fn); }

    void deferred_destroy(Entity p_entity) {
        deferred.commands.push_back({ WorldCommandBuffer::Op::DESTROY, 0, p_entity, 0 });
    }

    template <typename T>
    void deferred_add(Entity p_entity, const T& p_value = {}) {
        LUMEN_ERR_FAIL_COND(!_registered<T>());
        const uint32_t offset = (uint32_t)deferred.values.size();
        deferred.values.resize(offset + sizeof(T));
        std::memcpy(deferred.values.data() + offset, &p_value, sizeof(T));
        deferred.commands.push_back({ WorldCommandBuffer::Op::ADD, ComponentType<T>::id, p_entity, offset });
    }

    template <typename T>
    void deferred_remove(Entity p_entity) {
        LUMEN_ERR_FAIL_COND(!_registered<T>());
        deferred.commands.push_back({ WorldCommandBuffer::Op::REMOVE, ComponentType<T>::id, p_entity, 0 });
    }

    void deferred_flush();

    template <typename T>
    const std::vector<Entity>* entities_with() const {
        if (!_registered<T>()) return nullptr;
        return &_pool<T>(*this)->dense;
    }

    template <typename T>
    bool _registered() const {
        const uint32_t id = ComponentType<T>::id;
        return id < pools.size() && pools[id].set;
    }

    template <typename T, typename W>
    static auto* _pool(W& p_world) {
        using Pool = std::conditional_t<std::is_const_v<W>, const ComponentPool<T>, ComponentPool<T>>;
        return static_cast<Pool*>(p_world.pools[ComponentType<T>::id].set);
    }

    template <typename P>
    static auto* _fetch(P* p_pool, const SparseSet* p_driver, uint32_t p_pos, uint32_t p_index) {
        return p_pool->_at(static_cast<const SparseSet*>(p_pool) == p_driver ? p_pos : p_pool->find(p_index));
    }

    template <typename... Ts, typename W, typename Fn>
    static void _view(W& p_world, Fn& p_fn) {
        static_assert(sizeof...(Ts) > 0);
        if (!(p_world.template _registered<Ts>() && ...)) return;
        p_world.iterating++;
        [&](auto*... p_pools) {
            if constexpr (sizeof...(Ts) == 1) {
                auto* pool = (p_pools, ...);
                const uint32_t n = pool->size();
                for (uint32_t i = 0; i < n; i++) p_fn(pool->dense[i], *pool->_at(i));
            } else {
                const SparseSet* sets[] = { p_pools... };
                const SparseSet* driver = sets[0];
                for (const SparseSet* s : sets) { if (s->size() < driver->size()) driver = s; }
                ComponentMask required;
                (required.set(ComponentType<Ts>::id), ...);
                const uint32_t n = driver->size();
                for (uint32_t i = 0; i < n; i++) {
                    const Entity e = driver->dense[i];
                    if (!p_world.masks[e.index].contains(required)) continue;
                    p_fn(e, *_fetch(p_pools, driver, i, e.index)...);
                }
            }
        }(_pool<Ts>(p_world)...);
        p_world.iterating--;
    }

    void _remove_id(Entity p_entity, uint32_t p_id);
};

}