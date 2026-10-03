#ifndef GBUFFER_GLSL
#define GBUFFER_GLSL

vec3 linear_to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, greaterThan(c, vec3(0.0031308)));
}

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

vec4 gbuffer_encode_albedo(vec3 albedo, uint flags) {
    return vec4(linear_to_srgb(clamp(albedo, 0.0, 1.0)), float(flags & 3u) / 3.0);
}

void gbuffer_decode_albedo(vec4 g, out vec3 albedo, out uint flags) {
    albedo = srgb_to_linear(g.rgb);
    flags = uint(g.a * 3.0 + 0.5);
}

vec4 gbuffer_encode_material(float roughness, float metallic, float ao, uint shading_model) {
    return vec4(roughness, metallic, ao, float(shading_model & 0xffu) / 255.0);
}

void gbuffer_decode_material(vec4 g, out float roughness, out float metallic, out float ao, out uint shading_model) {
    roughness = g.r;
    metallic = g.g;
    ao = g.b;
    shading_model = uint(g.a * 255.0 + 0.5);
}

#endif