#pragma once
#include <core/world/world.h>
#include <cstdint>

namespace lumen {

struct EditorContext;

enum class SpawnType : uint8_t { EMPTY, FOLDER, MODEL, MODEL_GRID, COUNT };

static constexpr float SPAWN_DISTANCE = 10.0f;
static constexpr const char* SPAWN_NAMES[] = { "Entity", "Folder", "Model", "Model Grid" };

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, Entity p_parent, Guid p_model = {});
Entity editor_spawn_menu_items(EditorContext& ctx, Entity p_parent);

}
