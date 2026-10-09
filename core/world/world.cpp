#include <core/world/world.h>
#include <core/world/components.h>

namespace lumen {

Error World::initialize()
{
    using enum Error;

    component_register<EntityIdComponent>("Entity Id", false);
    component_register<NameComponent>("Name", false);
    component_register<TransformComponent>("Transform");
    component_register<MeshComponent>("Mesh");
    component_register<MeshGridComponent>("Mesh Grid");
    component_register<LightComponent>("Light");
    component_register<LightProfileComponent>("Light Profile");
    component_register<StaticTag>("Static", false);
    component_register<EditorFolderComponent>("Folder", false);
    component_register<EditorHiddenTag>("Hidden", false);
    static_tag_id = ComponentType<StaticTag>::id;

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
    by_guid.clear();
    static_version++;
    structure_version++;
    entity_version++;
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
    structure_version++;
    entity_version++;
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

Entity World::create_persistent(Guid p_guid)
{
    LUMEN_ERR_FAIL_COND_V(p_guid == Guid{}, ENTITY_NULL);
    LUMEN_ERR_FAIL_COND_V(by_guid.contains(p_guid), ENTITY_NULL);
    const Entity e = create();
    add<EntityIdComponent>(e, { p_guid });
    by_guid.emplace(p_guid, e);
    return e;
}

void World::destroy(Entity p_entity)
{
    LUMEN_ERR_FAIL_COND(iterating != 0);
    LUMEN_ERR_FAIL_COND(!valid(p_entity));
    if (const EntityIdComponent* id = try_get<EntityIdComponent>(p_entity)) by_guid.erase(id->guid);
    if (_is_static(p_entity)) static_version++;
    structure_version++;
    entity_version++;
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

Entity World::find(Guid p_guid) const
{
    const auto it = by_guid.find(p_guid);
    return it == by_guid.end() ? ENTITY_NULL : it->second;
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
                if (_is_static(c.entity)) static_version++;
                structure_version++;
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
    if (_is_static(p_entity)) static_version++;
    structure_version++;
    pools[p_id].remove_fn(pools[p_id].set, p_entity.index);
    mask.clear(p_id);
}

}