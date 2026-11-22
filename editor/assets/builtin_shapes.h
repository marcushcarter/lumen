#pragma once
#include <core/assets/guid.h>
#include <core/scene/components.h>
#include <cstdint>

namespace lumen {

struct Project;
struct Renderer;

enum class BuiltinMesh : uint8_t { CUBE, SPHERE, CYLINDER, CAPSULE, BOULDER_0, BOULDER_1, BOULDER_2, BOULDER_3, BOULDER_4, COUNT };

static constexpr uint32_t BUILTIN_BOULDER_VARIANTS = 5;
static constexpr float BUILTIN_CAPSULE_RADIUS = 0.2f;
static constexpr float BUILTIN_CAPSULE_HALF_HEIGHT = 0.35f;

Guid builtin_mesh(BuiltinMesh p_mesh);
bool builtin_hull(BuiltinMesh p_mesh);
CollisionShapeEntry builtin_collision(BuiltinMesh p_mesh);
float builtin_volume(BuiltinMesh p_mesh);
void builtin_shapes_prepare(const Project& p_project, Renderer& r_renderer);

}
