#include <core/game/entity_debug.h>
#include <core/animation/bone_debug.h>
#include <core/game/character.h>
#include <core/animation/dynamics.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <algorithm>

namespace lumen {

static const CollisionMeshCache::Entry* _model_collision(const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source)
{
    if (!p_source.meshes || !p_source.project) return nullptr;
    const ModelComponent* m = p_scene.try_get<ModelComponent>(p_entity);
    const uint32_t mesh_index = m ? p_source.renderer.models.mesh_index(m->model) : UINT32_MAX;
    if (!p_source.renderer.geometry.get(mesh_index)) return nullptr;
    const Guid mesh = p_source.renderer.geometry.mesh_guids[mesh_index];
    return p_source.meshes->get(mesh, p_source.project->content_path(mesh));
}

static void _draw_body(DebugDraw& r_draw, const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source)
{
    const ModelComponent* m = p_scene.try_get<ModelComponent>(p_entity);
    const PoseComponent* pose = p_scene.try_get<PoseComponent>(p_entity);
    if (!m || !pose) return;
    const LMeshRig* rig = p_source.renderer.geometry.rig(p_source.renderer.models.mesh_index(m->model));
    if (!rig) return;
    const mat4 world = p_scene.world_matrix(p_entity);
    const float scale = length(vec3(world[0]));
    const DebugDraw::Style style{ .color = DebugDraw::CYAN, .category = DebugCategory::COLLISION };
    for (const LMeshCollider& c : rig->dynamics.colliders) {
        if (c.joint >= pose->skin.size()) continue;
        const mat4 s = world * pose->skin[c.joint];
        r_draw.capsule(vec3(s * vec4(c.a, 1.0f)), vec3(s * vec4(c.b, 1.0f)), c.radius * scale, style);
    }
}

static void _draw_collision(DebugDraw& r_draw, const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source)
{
    if (const CollisionComponent* collision = p_scene.try_get<CollisionComponent>(p_entity)) {
        const mat4 world = p_scene.world_matrix(p_entity);
        const DebugDraw::Style style{ .color = collision_color(*collision), .category = DebugCategory::COLLISION };
        if (collision->source == CollisionSource::MESH) {
            if (const CollisionMeshCache::Entry* model = _model_collision(p_scene, p_entity, p_source)) debug_draw_lcollision(r_draw, world, model->data, model->edges, style, true);
        } else {
            for (uint32_t i = 0; i < std::min(collision->shape_count, COLLISION_MAX_SHAPES); i++) debug_draw_collision_shape(r_draw, world, collision->shapes[i], style);
        }
    }

    if (const LocomotionComponent* body = p_scene.try_get<LocomotionComponent>(p_entity)) {
        const CharacterStateComponent* state = p_scene.try_get<CharacterStateComponent>(p_entity);
        const vec3 base = state ? state->character.position : vec3(p_scene.world_matrix(p_entity)[3]);
        const float height = state && state->character.stance == Stance::CROUCHING ? body->crouch_height : body->height;
        const float radius = std::min(body->radius, height * 0.5f);
        const vec3 up(0.0f, 1.0f, 0.0f);
        r_draw.capsule(base + up * radius, base + up * (height - radius), radius, DebugDraw::Style{ .color = DebugDraw::YELLOW, .category = DebugCategory::COLLISION });
    }
}

void debug_draw_entity(DebugDraw& r_draw, const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source, uint32_t p_kinds)
{
    if (!p_scene.valid(p_entity) || p_scene.has<DisabledTag>(p_entity)) return;
    if (p_kinds & ENTITY_DEBUG_COLLISION) {
        _draw_collision(r_draw, p_scene, p_entity, p_source);
        _draw_body(r_draw, p_scene, p_entity, p_source);
    }
    if (p_kinds & ENTITY_DEBUG_BONES) debug_draw_entity_bones(r_draw, p_scene, p_source.renderer, p_entity);
    if (p_kinds & ENTITY_DEBUG_DYNAMICS) {
        if (const DynamicsComponent* d = p_scene.try_get<DynamicsComponent>(p_entity)) d->dynamics.draw(r_draw);
    }
}

void debug_draw_entities(DebugDraw& r_draw, const Scene& p_scene, const std::vector<Entity>& p_entities, const EntityDebugSource& p_source, uint32_t p_kinds)
{
    if (p_kinds == 0) return;
    for (const Entity e : p_entities) debug_draw_entity(r_draw, p_scene, e, p_source, p_kinds);
}

void debug_draw_scene(DebugDraw& r_draw, const Scene& p_scene, const EntityDebugSource& p_source, uint32_t p_kinds)
{
    if (p_kinds == 0) return;
    p_scene.view<TransformComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&) { debug_draw_entity(r_draw, p_scene, p_entity, p_source, p_kinds); });
}

}
