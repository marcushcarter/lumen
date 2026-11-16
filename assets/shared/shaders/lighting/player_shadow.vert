#version 460

#include "lighting/player_shadow_vertex.glsl"

void main() {
    ClusterStreams streams;
    uint local;
    Mesh mesh;
    gl_Position = player_shadow_vertex(streams, local, mesh);
}
