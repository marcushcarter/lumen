#version 460

#include "lighting/player_shadow_vertex.glsl"

layout(location = 0) out vec2 v_uv;

void main() {
    ClusterStreams streams;
    uint local;
    Mesh mesh;
    gl_Position = player_shadow_vertex(streams, local, mesh);
    v_uv = stream_uv(pc.geometry, streams, local, mesh.uv_min, mesh.uv_extent);
}
