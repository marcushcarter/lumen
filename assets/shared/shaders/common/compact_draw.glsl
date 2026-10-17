#ifndef COMPACT_DRAW_GLSL
#define COMPACT_DRAW_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

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

layout(buffer_reference, scalar) buffer CompactDrawArgs { CompactDrawCommand cmd[2]; uint tris[2]; uint done[2]; uint spill[2]; };
layout(buffer_reference, scalar) buffer CompactIndexBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer CompactPrimBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer CompactSpillBuffer { CompactSpillCommand cmd[]; };

const uint COMPACT_LOCAL_BITS = 8u;
const uint COMPACT_LOCAL_MASK = (1u << COMPACT_LOCAL_BITS) - 1u;

#endif