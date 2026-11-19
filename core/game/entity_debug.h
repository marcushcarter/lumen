#pragma once
#include <core/physics/collision_preview.h>
#include <core/rendering/debug_draw.h>
#include <core/scene/scene.h>
#include <vector>

namespace lumen {

struct Renderer;
struct Project;

enum EntityDebug : uint32_t
{
    ENTITY_DEBUG_COLLISION = 1u << 0,
    ENTITY_DEBUG_BONES = 1u << 1,
};

struct EntityDebugSource
{
    const Renderer& renderer;
    const Project* project = nullptr;
    CollisionMeshCache* meshes = nullptr;
};

void debug_draw_entity(DebugDraw& r_draw, const Scene& p_scene, Entity p_entity, const EntityDebugSource& p_source, uint32_t p_kinds);
void debug_draw_entities(DebugDraw& r_draw, const Scene& p_scene, const std::vector<Entity>& p_entities, const EntityDebugSource& p_source, uint32_t p_kinds);
void debug_draw_scene(DebugDraw& r_draw, const Scene& p_scene, const EntityDebugSource& p_source, uint32_t p_kinds);

}
