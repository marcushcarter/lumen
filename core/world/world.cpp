#include <core/world/world.h>
#include <core/world/components.h>

namespace lumen {

Error World::initialize()
{
    using enum Error;

    component_register<EntityIdComponent>("Entity Id", false);
    component_register<NameComponent>("Name", false);
    component_register<TransformComponent>("Transform");
    component_register<ModelComponent>("Model");
    component_register<ModelGridComponent>("Model Grid");
    component_register<LightComponent>("Light");
    component_register<LightProfileComponent>("Light Profile");
    component_register<StaticTag>("Static", false);
    component_register<EditorFolderComponent>("Folder", false);
    component_register<EditorHiddenTag>("Hidden", false);

    return OK;
}

void World::shutdown()
{
    unload();
    registry = Registry{};
    components.clear();
}

Error World::load()
{
    using enum Error;

    const vec3 target = vec3(0.0f, 0.5f, 0.0f);
    const float radius = 10.0f;
    const float height = 0.5f;
    const float angle = 0.0f;
    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    default_camera.position = eye;
    default_camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));

    active_camera = &default_camera;

    return OK;
}

void World::unload()
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    _deferred.clear();
    _by_guid.clear();
    environment = {};
    post = {};
    static_version++;
    structure_version++;
    entity_version++;
    registry.clear();
}

Entity World::create()
{
    structure_version++;
    entity_version++;
    return _from_id(registry.create());
}

Entity World::create_persistent(Guid p_guid)
{
    LUMEN_ERR_FAIL_COND_V(!p_guid.valid(), ENTITY_NULL);
    LUMEN_ERR_FAIL_COND_V(_by_guid.contains(p_guid.value), ENTITY_NULL);
    const Entity e = create();
    add<EntityIdComponent>(e, { p_guid });
    _by_guid.insert(p_guid.value, e.index);
    return e;
}

void World::destroy(Entity p_entity)
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    LUMEN_ERR_FAIL_COND(!valid(p_entity));
    if (const EntityIdComponent* id = try_get<EntityIdComponent>(p_entity)) _by_guid.erase(id->guid.value);
    if (_is_static(p_entity)) static_version++;
    structure_version++;
    entity_version++;
    registry.destroy(_to_id(p_entity));
}

Entity World::find(Guid p_guid) const
{
    const uint32_t index = _by_guid.get(p_guid.value);
    return index == IdMap::NONE ? ENTITY_NULL : entity_at(index);
}

Entity World::entity_at(uint32_t p_index) const
{
    if (p_index == Entity::INVALID_INDEX) return ENTITY_NULL;
    const Entity e{ p_index, (uint32_t)registry.current(Traits::construct(p_index, 0)) };
    return valid(e) ? e : ENTITY_NULL;
}

void World::deferred_flush()
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    std::vector<std::function<void(World&)>> commands = std::move(_deferred);
    _deferred.clear();
    for (std::function<void(World&)>& c : commands) c(*this);
}

}
