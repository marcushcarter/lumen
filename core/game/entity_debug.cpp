#include <core/game/entity_debug.h>
#include <core/animation/bone_debug.h>
#include <core/game/character.h>
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

static void _draw_collision(DebugDraw& r_draw, const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source, bool p_meshes)
{
    if (const CollisionComponent* collision = p_scene.try_get<CollisionComponent>(p_entity)) {
        const mat4 world = p_scene.world_matrix(p_entity);
        const DebugDraw::Style style{ .color = collision_color(*collision), .category = DebugCategory::COLLISION };
        const CollisionMeshCache::Entry* model = nullptr;
        bool model_looked = false;
        for (uint32_t i = 0; i < std::min(collision->shape_count, COLLISION_MAX_SHAPES); i++) {
            const CollisionShapeEntry& shape = collision->shapes[i];
            if (shape.type == CollisionShapeType::MODEL && !model_looked) {
                model = _model_collision(p_scene, p_entity, p_source);
                model_looked = true;
            }
            debug_draw_collision_shape(r_draw, world, shape, model, style, p_meshes);
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
    if (p_kinds & (ENTITY_DEBUG_COLLISION | ENTITY_DEBUG_MESH_COLLISION)) _draw_collision(r_draw, p_scene, p_entity, p_source, (p_kinds & ENTITY_DEBUG_MESH_COLLISION) != 0);
    if (p_kinds & ENTITY_DEBUG_BONES) debug_draw_entity_bones(r_draw, p_scene, p_source.renderer, p_entity);
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
