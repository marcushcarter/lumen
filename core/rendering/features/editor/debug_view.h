#pragma once
#include <cstdint>
#include <IconsFontAwesome6.h>

namespace lumen {

enum class DebugViewOp : uint32_t {
    COPY = 0,
    CLUSTERS,
    OVERDRAW,
    WORLD_NORMAL,
    UV_CHECKER,
    CLUSTER_STREAMING,
    TRIANGLES,
    TERRAIN_STREAMING,
    TERRAIN_LOD,
    BASE_COLOR,
    DEPTH,
    WATER_CONTEXT,
    DEPTH_NORMAL,
};

enum class DebugViewInputs : uint32_t {
    NONE = 0,
    SOURCE = 1u << 0,
    VISBUF = 1u << 1,
    DEPTH = 1u << 2,
    GEOMETRY = 1u << 3,
    TERRAIN = 1u << 4,
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
    uint32_t water = 0;
};

static constexpr DebugViewCategory DV_CAT_CLUSTERS = { ICON_FA_CUBES, "Cluster Visualization" };
static constexpr DebugViewCategory DV_CAT_GEOMETRY = { ICON_FA_SHAPES, "Geometry Inspection" };
static constexpr DebugViewCategory DV_CAT_TERRAIN = { ICON_FA_MOUNTAIN, "Terrain" };
static constexpr DebugViewCategory DV_CAT_BUFFERS = { ICON_FA_LAYER_GROUP, "Buffer Visualization" };
static constexpr DebugViewCategory DV_CAT_WATER = { ICON_FA_WATER, "Water" };

static constexpr DebugView DEBUG_VIEWS[] = {
    { ICON_FA_EYE, "Lit", nullptr, "DisplayColor", DebugViewOp::COPY, DebugViewInputs::SOURCE },

    { nullptr, "Base Color", &DV_CAT_BUFFERS, "G_Albedo", DebugViewOp::BASE_COLOR, DebugViewInputs::SOURCE | DebugViewInputs::DEPTH },
    { nullptr, "Scene Normal", &DV_CAT_BUFFERS, "G_Normal", DebugViewOp::WORLD_NORMAL, DebugViewInputs::SOURCE | DebugViewInputs::DEPTH },
    { nullptr, "Depth", &DV_CAT_BUFFERS, nullptr, DebugViewOp::DEPTH, DebugViewInputs::DEPTH },

    { nullptr, "UV Checker", &DV_CAT_GEOMETRY, nullptr, DebugViewOp::UV_CHECKER, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY | DebugViewInputs::TERRAIN },
    { nullptr, "Triangles", &DV_CAT_GEOMETRY, nullptr, DebugViewOp::TRIANGLES, DebugViewInputs::VISBUF | DebugViewInputs::TERRAIN },
    { nullptr, "Depth Normal", &DV_CAT_GEOMETRY, nullptr, DebugViewOp::DEPTH_NORMAL, DebugViewInputs::DEPTH },

    { nullptr, "Clusters", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::CLUSTERS, DebugViewInputs::VISBUF },
    { nullptr, "Cluster Streaming", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::CLUSTER_STREAMING, DebugViewInputs::VISBUF | DebugViewInputs::GEOMETRY },
    { nullptr, "Overdraw", &DV_CAT_CLUSTERS, nullptr, DebugViewOp::OVERDRAW, DebugViewInputs::DEPTH },

    { nullptr, "Terrain Streaming", &DV_CAT_TERRAIN, nullptr, DebugViewOp::TERRAIN_STREAMING, DebugViewInputs::VISBUF | DebugViewInputs::TERRAIN },
    { nullptr, "Terrain LOD", &DV_CAT_TERRAIN, nullptr, DebugViewOp::TERRAIN_LOD, DebugViewInputs::VISBUF | DebugViewInputs::TERRAIN },

    { nullptr, "Caustics", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 1 },
    { nullptr, "Depth", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 2 },
    { nullptr, "Speed", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 3 },
    { nullptr, "Foam", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 4 },
    { nullptr, "Vorticity", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 5 },
    { nullptr, "Fresnel", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 6 },
    { nullptr, "View Thickness", &DV_CAT_WATER, nullptr, DebugViewOp::WATER_CONTEXT, DebugViewInputs::DEPTH, 7 },
};
static constexpr int DEBUG_VIEW_COUNT = (int)(sizeof(DEBUG_VIEWS) / sizeof(DEBUG_VIEWS[0]));

}
