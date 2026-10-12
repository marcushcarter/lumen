#ifndef OUTLINE_COMMON_GLSL
#define OUTLINE_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint OUTLINE_NO_ENTITY = 0xFFFFFFFFu;
const uint OUTLINE_HOVER_BIT = 1u << 31;

layout(buffer_reference, scalar) buffer OutlineArgsBuffer {
    uvec3 dispatch;
    uint hovered;
    uvec4 rect;
};

#endif
