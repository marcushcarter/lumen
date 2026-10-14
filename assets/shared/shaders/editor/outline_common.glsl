#ifndef OUTLINE_COMMON_GLSL
#define OUTLINE_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer SelectionWordsBuffer { uint word_count; uint words[]; };

layout(buffer_reference, scalar) buffer SelectionClustersBuffer {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
    uint data[];
};

bool selection_contains(SelectionWordsBuffer p_mask, uint p_entity) {
    uint w = p_entity >> 5u;
    return w < p_mask.word_count && ((p_mask.words[w] >> (p_entity & 31u)) & 1u) != 0u;
}

#endif
