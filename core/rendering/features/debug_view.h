#pragma once
#include <cstdint>
#include <IconsFontAwesome6.h>

namespace lumen {

enum class DebugViewOp : uint32_t {
    COPY = 0,
};

enum class DebugViewInputs : uint32_t {
    NONE = 0,
    SOURCE = 1u << 0,
};

constexpr DebugViewInputs operator|(DebugViewInputs a, DebugViewInputs b) { return (DebugViewInputs)((uint32_t)a | (uint32_t)b); }
constexpr bool operator&(DebugViewInputs a, DebugViewInputs b) { return ((uint32_t)a & (uint32_t)b) != 0; }

struct DebugView {
    const char* name;
    const char* category;
    const char* image;
    DebugViewOp op;
    DebugViewInputs inputs;
};

static constexpr DebugView DEBUG_VIEWS[] = {
    { ICON_FA_LIGHTBULB "  Lit", "", "G_Albedo", DebugViewOp::COPY, DebugViewInputs::SOURCE },
    { ICON_FA_IMAGE "  Unlit", "", "G_Albedo", DebugViewOp::COPY, DebugViewInputs::SOURCE },
};
static constexpr int DEBUG_VIEW_COUNT = (int)(sizeof(DEBUG_VIEWS) / sizeof(DEBUG_VIEWS[0]));

/*

Lit
Unlit
Wireframe

BUFFERS VISUALIZATION
Buffer Overview - all buffers
Base Color
Metallic
Roughness
Specular
World Normal
Shading Model
Material AO
Ambient Occlusion
Scene Depth

GEOMETRY
LOD coloration
Triangles / Primitives
Clusters
Instances
Overdraw (lighter = more overdraw)

GIBS VISUALIZATION
Direct Light
Indirect Light
Surfels Vis
Light complexity (blue -> red cluster light culling thing)

*/

}