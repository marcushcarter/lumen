#ifndef SUN_RECEIVER_COMMON_GLSL
#define SUN_RECEIVER_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SUN_RECEIVER_SIZE = 128u;
const uint SUN_RECEIVER_LEVELS = 8u;
const uint SUN_RECEIVER_CELLS = (SUN_RECEIVER_SIZE * SUN_RECEIVER_SIZE * 4u - 1u) / 3u;

layout(buffer_reference, scalar) coherent buffer SunReceiverBuffer { uint data[]; };

uint sun_receiver_level_offset(uint p_level) {
    const uint size = SUN_RECEIVER_SIZE >> p_level;
    return (4u * (SUN_RECEIVER_SIZE * SUN_RECEIVER_SIZE - size * size)) / 3u;
}

#endif
