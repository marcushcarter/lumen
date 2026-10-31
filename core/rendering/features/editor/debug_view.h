#pragma once
#include <cstdint>
#include <IconsFontAwesome6.h>

namespace lumen {

enum class DebugViewOp : uint32_t {
    COPY = 0,
    DEPTH_NORMAL,
    DEPTH,
    TRIANGLES,
    CLUSTERS,
    INSTANCES,
    MATERIAL_ID,
    VELOCITY,
    CLAY,
    OVERDRAW,
    WIREFRAME,
    WORLD_NORMAL,
    LOD_LEVEL,
    CLUSTER_ERROR,
    TRIANGLE_DENSITY,
    UV_CHECKER,
};

enum class DebugViewInputs : uint32_t {
    NONE = 0,
    SOURCE = 1u << 0,
    VISBUF = 1u << 1,
    DEPTH = 1u << 2,
    GEOMETRY = 1u << 3,
};

constexpr DebugViewInputs operator|(DebugViewInputs a, DebugViewInputs b) { return (DebugViewInputs)((uint32_t)a | (uint32_t)b); }
constexpr bool operator&(DebugViewInputs a, DebugViewInputs b) { return ((uint32_t)a & (uint32_t)b) != 0; }

struct DebugViewCategory
{
    const char* icon;
    const char* name;
};

struct DebugView
{
    const char* icon;
    const char* name;
    const DebugViewCategory* category;
    const char* image;
    DebugViewOp op;
    DebugViewInputs inputs;
};

static constexpr DebugViewCategory DV_CAT_CLUSTERS = { ICON_FA_CUBES, "Cluster Visualization" };
static constexpr DebugViewCategory DV_CAT_GEOMETRY = { ICON_FA_SHAPES, "Geometry Inspection" };
static constexpr DebugViewCategory DV_CAT_BUFFERS = { ICON_FA_LAYER_GROUP, "Buffer Visualization" };

static constexpr DebugView DEBUG_VIEWS[] = {
    { ICON_FA_EYE, "Lit", nullptr, "DisplayColor", DebugViewOp::COPY, DebugViewInputs::SOURCE },
    { ICON_FA_DRAW_POLYGON, "Wireframe", nullptr, nullptr, DebugViewOp::WIREFRAME, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },

    { nullptr, "Triangles", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::TRIANGLES, DebugViewInputs::VISBUF },
    { nullptr, "Clusters", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::CLUSTERS, DebugViewInputs::VISBUF },
    { nullptr, "Instances", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::INSTANCES, DebugViewInputs::VISBUF },
    { nullptr, "Material ID", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::MATERIAL_ID, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    { nullptr, "LOD Level", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::LOD_LEVEL, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    { nullptr, "Cluster Error", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::CLUSTER_ERROR, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    { nullptr, "Triangle Density", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::TRIANGLE_DENSITY, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    { nullptr, "Overdraw", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::OVERDRAW, DebugViewInputs::DEPTH },
    // occlusion phase
    // hiz mip

    { nullptr, "Depth Normal", &DV_CAT_GEOMETRY, "G_Depth", DebugViewOp::DEPTH_NORMAL, DebugViewInputs::SOURCE },
    { nullptr, "Clay", &DV_CAT_GEOMETRY, nullptr, DebugViewOp::CLAY, DebugViewInputs::VISBUF },
    { nullptr, "UV Checker", &DV_CAT_GEOMETRY, nullptr, DebugViewOp::UV_CHECKER, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    // random color
    // zebra
    // mip level
    // texel density

    { nullptr, "World Normal", &DV_CAT_BUFFERS, "G_Normal", DebugViewOp::WORLD_NORMAL, DebugViewInputs::SOURCE | DebugViewInputs::DEPTH },
    { nullptr, "Velocity", &DV_CAT_BUFFERS, "G_Motion", DebugViewOp::VELOCITY, DebugViewInputs::SOURCE },
    { nullptr, "Scene Depth", &DV_CAT_BUFFERS, "G_Depth", DebugViewOp::DEPTH, DebugViewInputs::SOURCE },
};
static constexpr int DEBUG_VIEW_COUNT = (int)(sizeof(DEBUG_VIEWS) / sizeof(DEBUG_VIEWS[0]));

}
