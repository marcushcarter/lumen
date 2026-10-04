// assets/shared/shaders/common/hiz.glsl
#ifndef HIZ_GLSL
#define HIZ_GLSL

#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_hiz_textures[];

bool _hiz_rect_occluded(uint p_hiz, uint p_mips, vec2 p_screen, vec2 p_lo, vec2 p_hi, float p_nearest)
{
    if (p_nearest >= 1.0) return false;
    if (any(lessThan(p_hi, vec2(-1.0))) || any(greaterThan(p_lo, vec2(1.0)))) return true;

    vec2 px_lo = clamp((p_lo * 0.5 + 0.5) * p_screen, vec2(0.0), p_screen - 1.0);
    vec2 px_hi = clamp((p_hi * 0.5 + 0.5) * p_screen, vec2(0.0), p_screen - 1.0);
    ivec2 t_lo = ivec2(px_lo) >> 1;
    ivec2 t_hi = ivec2(px_hi) >> 1;

    int extent = max(t_hi.x - t_lo.x, t_hi.y - t_lo.y);
    int mip = extent > 1 ? findMSB(extent - 1) : 0;
    mip = min(mip, int(p_mips) - 1);

    ivec2 size = textureSize(u_hiz_textures[p_hiz], mip);
    ivec2 a = min(t_lo >> mip, size - 1);
    ivec2 b = min(t_hi >> mip, size - 1);

    float d = 3.402823466e38;
    for (int y = a.y; y <= b.y; y++)
        for (int x = a.x; x <= b.x; x++)
            d = min(d, texelFetch(u_hiz_textures[p_hiz], ivec2(x, y), mip).r);
    return p_nearest < d;
}

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
    return _hiz_rect_occluded(p_hiz, p_mips, p_screen, lo, hi, nearest);
}

bool hiz_occluded_box(uint p_hiz, uint p_mips, vec2 p_screen, mat4 p_view_proj, mat4 p_model, vec3 p_min, vec3 p_extent)
{
    mat4 m = p_view_proj * p_model;
    vec4 c = m * vec4(p_min, 1.0);
    vec4 ax = m[0] * p_extent.x;
    vec4 ay = m[1] * p_extent.y;
    vec4 az = m[2] * p_extent.z;

    vec2 lo = vec2(1e30);
    vec2 hi = vec2(-1e30);
    float nearest = 0.0;
    for (uint i = 0u; i < 8u; i++) {
        vec4 p = c + (((i & 1u) != 0u) ? ax : vec4(0.0)) + (((i & 2u) != 0u) ? ay : vec4(0.0)) + (((i & 4u) != 0u) ? az : vec4(0.0));
        if (p.w <= 1e-6) return false;
        vec3 ndc = p.xyz / p.w;
        lo = min(lo, ndc.xy);
        hi = max(hi, ndc.xy);
        nearest = max(nearest, ndc.z);
    }
    return _hiz_rect_occluded(p_hiz, p_mips, p_screen, lo, hi, nearest);
}

#endif