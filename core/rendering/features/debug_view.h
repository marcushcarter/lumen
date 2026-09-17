#pragma once
#include <cstdint>
#include <IconsFontAwesome6.h>

namespace lumen {

enum class DebugViewOp : uint32_t {
    COPY = 0,
    NORMAL,
    DEPTH,
    TRIANGLES,
    CLUSTERS,
    INSTANCES,
    MATERIAL_ID,
    VELOCITY,

    // WIREFRAME,
    // DIRECT,
    // INDIRECT,

    // SHADING_MODEL,
    // METALLIC,
    // ROUGHNESS,
    // LUMEN,

    // OVERDRAW,

    // SURFELS,
    // LIGHT_COMPLEXITY,
};

enum class DebugViewInputs : uint32_t {
    NONE = 0,
    SOURCE = 1u << 0,
    VISBUF = 1u << 1,
    // SHDMOD = 1u << 2,
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

static constexpr const char* DV_CAT_BUFFERS = ICON_FA_IMAGE "  Buffer Visualization";
static constexpr const char* DV_CAT_GEOMETRY = ICON_FA_IMAGE "  Geometry Visualization";

static constexpr DebugView DEBUG_VIEWS[] = {
    // { ICON_FA_LIGHTBULB "  Lit", "", "G_Albedo", DebugViewOp::COPY, DebugViewInputs::SOURCE },
    { ICON_FA_IMAGE "  Unlit", "", "G_Albedo", DebugViewOp::COPY, DebugViewInputs::SOURCE },
    
    { "Base Color", DV_CAT_BUFFERS, "G_Albedo", DebugViewOp::COPY, DebugViewInputs::SOURCE },
    { "World Normal", DV_CAT_BUFFERS, "G_Depth", DebugViewOp::NORMAL, DebugViewInputs::SOURCE },
    { "Velocity", DV_CAT_BUFFERS, "G_Motion", DebugViewOp::VELOCITY, DebugViewInputs::SOURCE },
    { "Scene Depth", DV_CAT_BUFFERS, "G_Depth", DebugViewOp::DEPTH, DebugViewInputs::SOURCE },
    
    { "Triangles", DV_CAT_GEOMETRY, nullptr, DebugViewOp::TRIANGLES, DebugViewInputs::VISBUF },
    { "Clusters", DV_CAT_GEOMETRY, nullptr, DebugViewOp::CLUSTERS, DebugViewInputs::VISBUF },
    { "Instances", DV_CAT_GEOMETRY, nullptr, DebugViewOp::INSTANCES, DebugViewInputs::VISBUF },
    { "Material ID", DV_CAT_GEOMETRY, nullptr, DebugViewOp::MATERIAL_ID, DebugViewInputs::VISBUF },
};
static constexpr int DEBUG_VIEW_COUNT = (int)(sizeof(DEBUG_VIEWS) / sizeof(DEBUG_VIEWS[0]));

/*

Lit
Unlit
Wireframe
Direct Lighting / Direct Only
Indirect Lighting / Indirect Only

BUFFERS VISUALIZATION
Base Color
World Normal
Scene Depth

GEOMETRY VISUALIZATION
Triangles
Clusters
Primitives
Insatnces
Overdraw
Material ID

RADIANCE
Surfels
Radiance Cache
Indirect Lighting / Indirect Only

*/

}