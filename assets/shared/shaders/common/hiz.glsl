#ifndef HIZ_GLSL
#define HIZ_GLSL

#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_hiz_textures[];

bool hiz_occluded(uint p_hiz, uint p_mips, vec2 p_screen, mat4 p_view_proj, vec3 p_center, float p_radius)
{
    vec4 c = p_view_proj * vec4(p_center, 1.0);
    vec4 ax = p_view_proj[0] * p_radius;
    vec4 ay = p_view_proj[1] * p_radius;
    vec4 az = p_view_proj[2] * p_radius;

    vec2 lo = vec2(1e30);
    vec2 hi = vec2(-1e30);
    float nearest = 0.0;
    for (uint i = 0u; i < 8u; i++) {
        vec4 p = c + (((i & 1u) != 0u) ? ax : -ax) + (((i & 2u) != 0u) ? ay : -ay) + (((i & 4u) != 0u) ? az : -az);
        if (p.w <= 1e-6) return false;
        vec3 ndc = p.xyz / p.w;
        lo = min(lo, ndc.xy);
        hi = max(hi, ndc.xy);
        nearest = max(nearest, ndc.z);
    }
    if (nearest >= 1.0) return false;
    if (any(lessThan(hi, vec2(-1.0))) || any(greaterThan(lo, vec2(1.0)))) return false;

    vec2 px_lo = clamp((lo * 0.5 + 0.5) * p_screen, vec2(0.0), p_screen - 1.0);
    vec2 px_hi = clamp((hi * 0.5 + 0.5) * p_screen, vec2(0.0), p_screen - 1.0);
    ivec2 t_lo = ivec2(px_lo) >> 1;
    ivec2 t_hi = ivec2(px_hi) >> 1;

    int extent = max(t_hi.x - t_lo.x, t_hi.y - t_lo.y);
    int mip = extent > 0 ? findMSB(extent - 1) + 1 : 0;
    mip = min(mip, int(p_mips) - 1);

    ivec2 size = textureSize(u_hiz_textures[p_hiz], mip);
    ivec2 a = min(t_lo >> mip, size - 1);
    ivec2 b = min(t_hi >> mip, size - 1);

    float d = min(min(texelFetch(u_hiz_textures[p_hiz], a, mip).r, texelFetch(u_hiz_textures[p_hiz], ivec2(b.x, a.y), mip).r), min(texelFetch(u_hiz_textures[p_hiz], ivec2(a.x, b.y), mip).r, texelFetch(u_hiz_textures[p_hiz], b, mip).r));
    return nearest < d;
}

#endif