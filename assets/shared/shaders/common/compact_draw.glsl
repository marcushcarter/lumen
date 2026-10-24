#ifndef COMPACT_DRAW_GLSL
#define COMPACT_DRAW_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// Raster bins: bit 0 = two-sided (no culling), bit 1 = masked (alpha test). Drawn in index order.
const uint RASTER_BIN_OPAQUE = 0u;
const uint RASTER_BIN_TWO_SIDED = 1u;
const uint RASTER_BIN_MASKED = 2u;
const uint RASTER_BIN_COUNT = 4u;
const uint COMPACT_CMD_COUNT = 2u * RASTER_BIN_COUNT;

struct CompactDrawCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

struct CompactSpillCommand {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

// cmd/tris are indexed by phase * RASTER_BIN_COUNT + bin.
layout(buffer_reference, scalar) buffer CompactDrawArgs { CompactDrawCommand cmd[COMPACT_CMD_COUNT]; uint tris[COMPACT_CMD_COUNT]; uint done[2]; uint spill[2]; };
layout(buffer_reference, scalar) buffer CompactIndexBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer CompactPrimBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer CompactMaterialBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer CompactSpillBuffer { CompactSpillCommand cmd[]; };

const uint COMPACT_LOCAL_BITS = 8u;
const uint COMPACT_LOCAL_MASK = (1u << COMPACT_LOCAL_BITS) - 1u;

// Spill first_instance: visible slot in the low bits, mask of the bins that spilled above them.
const uint COMPACT_SPILL_SLOT_BITS = 22u;
const uint COMPACT_SPILL_SLOT_MASK = (1u << COMPACT_SPILL_SLOT_BITS) - 1u;

#endif