// editor/world/editor_spawn.h
#pragma once
#include <core/world/world.h>
#include <cstdint>

namespace lumen {

struct EditorContext;

enum class SpawnType : uint8_t { EMPTY, MESH, MESH_GRID, COUNT };

static constexpr float SPAWN_DISTANCE = 10.0f;
static constexpr const char* SPAWN_NAMES[] = { "Entity", "Mesh", "Mesh Grid" };

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, uint32_t p_folder, Guid p_mesh = {});
Entity editor_spawn_menu_items(EditorContext& ctx, uint32_t p_folder);

}