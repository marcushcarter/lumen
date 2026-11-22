#pragma once
#include <core/scene/scene.h>
#include <cstdint>

namespace lumen {

struct EditorContext;

enum class SpawnType : uint8_t { EMPTY, FOLDER, MESH, COUNT };

static constexpr float SPAWN_DISTANCE = 10.0f;
static constexpr const char* SPAWN_NAMES[] = { "Entity", "Folder", "Mesh" };

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, Entity p_parent, Guid p_mesh = {}, const vec3* p_position = nullptr);
Entity editor_spawn_menu_items(EditorContext& ctx, Entity p_parent);

}
