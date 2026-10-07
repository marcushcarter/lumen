#include <core/world/world.h>
#include <core/world/components.h>

namespace lumen {

Error World::initialize()
{
    using enum Error;

    component_register<EntityIdComponent>();
    component_register<TransformComponent>();
    component_register<MeshComponent>();
    component_register<StaticTag>();

    return OK;
}

void World::shutdown()
{
    unload();
    for (PoolSlot& slot : pools) {
        if (slot.set) slot.free_fn(slot.set);
    }
    pools.clear();
    generations.clear();
    masks.clear();
    free_list.clear();
}

Error World::load()
{
    using enum Error;

    const vec3 target = vec3(0.0f, 50.0f, 0.0f);
    const float radius = 1000.0f;
    const float height = 50.0f;
    const float angle = 0.0f;
    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    default_camera.position = eye;
    default_camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
    
    
    active_camera = &default_camera;

    return OK;
}

void World::unload()
{
    LUMEN_ERR_FAIL_COND(iterating != 0);
    deferred.clear();
    for (PoolSlot& slot : pools) {
        if (slot.set) slot.clear_fn(slot.set);
    }
    free_list.clear();
    for (uint32_t i = (uint32_t)generations.size(); i-- > 0;) {
        generations[i]++;
        masks[i].reset();
        free_list.push_back(i);
    }
}

Entity World::create()
{
    if (!free_list.empty()) {
        const uint32_t i = free_list.back();
        free_list.pop_back();
        return { i, generations[i] };
    }
    const uint32_t i = (uint32_t)generations.size();
    generations.push_back(0);
    masks.push_back({});
    return { i, 0 };
}

void World::destroy(Entity p_entity)
{
    LUMEN_ERR_FAIL_COND(iterating != 0);
    LUMEN_ERR_FAIL_COND(!valid(p_entity));
    ComponentMask& mask = masks[p_entity.index];
    for (uint32_t w = 0; w < ComponentMask::WORDS; w++) {
        uint64_t bits = mask.words[w];
        while (bits) {
            const uint32_t id = w * 64 + (uint32_t)std::countr_zero(bits);
            pools[id].remove_fn(pools[id].set, p_entity.index);
            bits &= bits - 1;
        }
    }
    mask.reset();
    generations[p_entity.index]++;
    free_list.push_back(p_entity.index);
}

void World::deferred_flush()
{
    using Op = WorldCommandBuffer::Op;

    LUMEN_ERR_FAIL_COND(iterating != 0);
    for (const WorldCommandBuffer::Command& c : deferred.commands) {
        if (!valid(c.entity)) continue;
        switch (c.op) {
            case Op::DESTROY: destroy(c.entity); break;
            case Op::ADD:
                masks[c.entity.index].set(c.type);
                pools[c.type].add_bytes_fn(pools[c.type].set, c.entity, deferred.values.data() + c.value_offset);
                break;
            case Op::REMOVE: _remove_id(c.entity, c.type); break;
        }
    }
    deferred.clear();
}

void World::_remove_id(Entity p_entity, uint32_t p_id)
{
    ComponentMask& mask = masks[p_entity.index];
    if (!mask.test(p_id)) return;
    pools[p_id].remove_fn(pools[p_id].set, p_entity.index);
    mask.clear(p_id);
}

}