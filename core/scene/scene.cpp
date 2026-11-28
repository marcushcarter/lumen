#include <core/scene/scene.h>
#include <core/scene/components.h>
#include <core/scene/component_serialize.h>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>

namespace lumen {

bool character_is_player(const Scene& p_world, Entity p_entity)
{
    const MovementComponent* m = p_world.try_get<MovementComponent>(p_entity);
    return m && m->player;
}

Error Scene::initialize()
{
    using enum Error;

    component_register<EntityIdComponent>("Entity Id", false);
    component_register_saved<NameComponent>("Name", "name", false);
    component_register_saved<ParentComponent>("Parent", "parent", false);
    component_register_saved<TransformComponent>("Transform", "transform");
    component_register_saved<MeshComponent>("Mesh", "mesh");
    component_register<PoseComponent>("Pose", false);
    component_register<InitialVelocityComponent>("Initial Velocity", false);
    component_register_saved<CollisionComponent>("Collision", "collision");
    component_register_saved<MovementComponent>("Movement", "movement");
    component_register_saved<CameraComponent>("Camera", "camera");
    component_register_saved<CharacterComponent>("Character", "character");
    component_register_saved<LookAtComponent>("Look At", "look_at");
    component_register_saved<AttentionPointComponent>("Attention Point", "attention_point");
    component_register_saved<WaterSourceComponent>("Water Source", "water_source");
    component_register_saved<TemplateInstanceComponent>("Template", "template", false);
    component_register<TemplatePartComponent>("Template Part", false);
    component_register_saved<StaticTag>("Static", "static", false);
    component_register_saved<DisabledTag>("Disabled", "disabled", false);
    component_register<EditorExpandedTag>("Expanded", false);

    return OK;
}

void Scene::shutdown()
{
    unload();
    registry = Registry{};
    components.clear();
}

Error Scene::load()
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

void Scene::unload()
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    _deferred.clear();
    _by_guid.clear();
    environment = {};
    shadows = {};
    physics = {};
    terrain = {};
    water = {};
    if (track_changes) changed_all = true;
    static_version++;
    structure_version++;
    entity_version++;
    registry.clear();
}

Entity Scene::create()
{
    structure_version++;
    entity_version++;
    return _from_id(registry.create());
}

Entity Scene::create_persistent(Guid p_guid)
{
    LUMEN_ERR_FAIL_COND_V(!p_guid.valid(), ENTITY_NULL);
    LUMEN_ERR_FAIL_COND_V(_by_guid.contains(p_guid.value), ENTITY_NULL);
    const Entity e = create();
    add<EntityIdComponent>(e, { p_guid });
    _by_guid.insert(p_guid.value, e.index);
    return e;
}

void Scene::destroy(Entity p_entity)
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    LUMEN_ERR_FAIL_COND(!valid(p_entity));
    if (const EntityIdComponent* id = try_get<EntityIdComponent>(p_entity)) {
        _by_guid.erase(id->guid.value);
        if (track_changes) destroyed.push_back(id->guid.value);
    }
    if (_is_static(p_entity)) static_version++;
    structure_version++;
    entity_version++;
    registry.destroy(_to_id(p_entity));
}

Entity Scene::parent(Entity p_entity) const
{
    const ParentComponent* p = try_get<ParentComponent>(p_entity);
    return p && p->parent.valid() ? find(p->parent) : ENTITY_NULL;
}

bool Scene::is_ancestor(Entity p_ancestor, Entity p_entity) const
{
    uint32_t steps = 0;
    for (Entity e = parent(p_entity); e != ENTITY_NULL && steps < MAX_DEPTH; e = parent(e), steps++) {
        if (e == p_ancestor) return true;
    }
    return false;
}

bool Scene::set_parent(Entity p_entity, Entity p_parent)
{
    if (!valid(p_entity)) return false;
    if (p_parent == p_entity || (p_parent != ENTITY_NULL && (!valid(p_parent) || is_ancestor(p_entity, p_parent)))) return false;
    const EntityIdComponent* id = p_parent != ENTITY_NULL ? try_get<EntityIdComponent>(p_parent) : nullptr;
    if (p_parent != ENTITY_NULL && !id) return false;
    if (parent(p_entity) == p_parent) return false;

    const mat4 world = world_matrix(p_entity);
    if (id) add<ParentComponent>(p_entity, { id->guid });
    else remove<ParentComponent>(p_entity);
    if (has<TransformComponent>(p_entity)) set_world_matrix(p_entity, world);
    touch(p_entity);
    return true;
}

bool Scene::is_folder(Entity p_entity) const
{
    if (!valid(p_entity) || has<TransformComponent>(p_entity)) return false;
    for (const ComponentInfo& info : components) {
        if (!info.key || !info.has(*this, p_entity)) continue;
        const std::string_view key = info.key;
        if (key != "name" && key != "parent" && key != "disabled" && key != "expanded") return false;
    }
    return true;
}

bool Scene::has_children(Entity p_entity) const
{
    const EntityIdComponent* id = try_get<EntityIdComponent>(p_entity);
    if (!id) return false;
    for (const auto [e, p] : registry.view<ParentComponent>().each()) {
        if (p.parent == id->guid) return true;
    }
    return false;
}

bool Scene::has_static_descendant(Entity p_entity) const
{
    std::vector<Entity> pending;
    std::vector<Entity> kids;
    children(p_entity, pending);
    while (!pending.empty()) {
        const Entity e = pending.back();
        pending.pop_back();
        if (_is_static(e)) return true;
        children(e, kids);
        pending.insert(pending.end(), kids.begin(), kids.end());
    }
    return false;
}

void Scene::children(Entity p_entity, std::vector<Entity>& r_children) const
{
    r_children.clear();
    const EntityIdComponent* id = try_get<EntityIdComponent>(p_entity);
    if (!id) return;
    view<ParentComponent>([&](Entity e, const ParentComponent& p) {
        if (p.parent == id->guid) r_children.push_back(e);
    });
}

void Scene::subtree(Entity p_entity, std::vector<Entity>& r_entities) const
{
    r_entities.clear();
    if (!valid(p_entity)) return;
    r_entities.push_back(p_entity);
    std::vector<Entity> kids;
    for (size_t i = 0; i < r_entities.size(); i++) {
        children(r_entities[i], kids);
        r_entities.insert(r_entities.end(), kids.begin(), kids.end());
    }
}

void Scene::destroy_subtree(Entity p_entity)
{
    std::vector<Entity> all;
    subtree(p_entity, all);
    for (size_t i = all.size(); i-- > 0;) destroy(all[i]);
}

mat4 transform_matrix(const TransformComponent& p_transform)
{
    return translate(mat4(1.0f), p_transform.position) * mat4_cast(p_transform.rotation) * scale(mat4(1.0f), p_transform.scale);
}

mat4 Scene::local_matrix(Entity p_entity) const
{
    const TransformComponent* xf = try_get<TransformComponent>(p_entity);
    return xf ? transform_matrix(*xf) : mat4(1.0f);
}

mat4 Scene::world_matrix(Entity p_entity) const
{
    mat4 m = local_matrix(p_entity);
    uint32_t steps = 0;
    for (Entity e = parent(p_entity); e != ENTITY_NULL && steps < MAX_DEPTH; e = parent(e), steps++) m = local_matrix(e) * m;
    return m;
}

mat4 Scene::parent_matrix(Entity p_entity) const
{
    const Entity p = parent(p_entity);
    return p == ENTITY_NULL ? mat4(1.0f) : world_matrix(p);
}

TransformComponent transform_from_matrix(const mat4& p_matrix)
{
    TransformComponent xf;
    xf.position = vec3(p_matrix[3]);
    xf.scale = vec3(length(vec3(p_matrix[0])), length(vec3(p_matrix[1])), length(vec3(p_matrix[2])));
    if (determinant(mat3(p_matrix)) < 0.0f) xf.scale.x = -xf.scale.x;
    const vec3 s = max(abs(xf.scale), vec3(1e-8f)) * sign(xf.scale + vec3(1e-20f));
    const mat3 rot(vec3(p_matrix[0]) / s.x, vec3(p_matrix[1]) / s.y, vec3(p_matrix[2]) / s.z);
    xf.rotation = normalize(quat_cast(rot));
    return xf;
}

TransformComponent Scene::world_transform(Entity p_entity) const
{
    return transform_from_matrix(world_matrix(p_entity));
}

void Scene::set_world_matrix(Entity p_entity, const mat4& p_world)
{
    TransformComponent* xf = try_get<TransformComponent>(p_entity);
    if (!xf) return;
    *xf = transform_from_matrix(inverse(parent_matrix(p_entity)) * p_world);
    touch(p_entity);
}

Entity Scene::find(Guid p_guid) const
{
    const uint32_t index = _by_guid.get(p_guid.value);
    return index == IdMap::NONE ? ENTITY_NULL : entity_at(index);
}

Entity Scene::entity_at(uint32_t p_index) const
{
    if (p_index == Entity::INVALID_INDEX) return ENTITY_NULL;
    const Entity e{ p_index, (uint32_t)registry.current(Traits::construct(p_index, 0)) };
    return valid(e) ? e : ENTITY_NULL;
}

void Scene::deferred_flush()
{
    LUMEN_ERR_FAIL_COND(_iterating != 0);
    std::vector<std::function<void(Scene&)>> commands = std::move(_deferred);
    _deferred.clear();
    for (std::function<void(Scene&)>& c : commands) c(*this);
}

}
