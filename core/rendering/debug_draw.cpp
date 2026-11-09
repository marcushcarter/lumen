#include <core/rendering/debug_draw.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

DebugDraw& debug_draw()
{
    static DebugDraw instance;
    return instance;
}

static void _basis(const vec3& p_n, vec3& r_u, vec3& r_v)
{
    const vec3 up = std::abs(p_n.y) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
    r_u = normalize(cross(up, p_n));
    r_v = cross(p_n, r_u);
}

uint32_t DebugDraw::_flags(const Style& p_style)
{
    return ((uint32_t)p_style.category & CATEGORY_MASK) | (p_style.xray ? FLAG_XRAY : 0u);
}

void DebugDraw::_push(const vec3& p_a, const vec3& p_b, uint32_t p_color, uint32_t p_flags, float p_duration)
{
    const DebugLine l{ p_a, p_color, p_b, p_flags };
    if (p_duration > 0.0f) {
        if (timed_lines.size() >= MAX_LINES) return;
        timed_lines.push_back(l);
        timed_left.push_back(p_duration);
        return;
    }
    if (frame_lines.size() >= MAX_LINES) return;
    frame_lines.push_back(l);
}

void DebugDraw::_arc(const vec3& p_center, const vec3& p_x, const vec3& p_y, float p_radius, float p_from, float p_to, uint32_t p_segments, const Style& p_style)
{
    const uint32_t flags = _flags(p_style);
    const float step = (p_to - p_from) / (float)p_segments;
    vec3 prev = p_center + (p_x * std::cos(p_from) + p_y * std::sin(p_from)) * p_radius;
    for (uint32_t i = 1; i <= p_segments; i++) {
        const float t = p_from + step * (float)i;
        const vec3 next = p_center + (p_x * std::cos(t) + p_y * std::sin(t)) * p_radius;
        _push(prev, next, p_style.color, flags, p_style.duration);
        prev = next;
    }
}

void DebugDraw::point(const vec3& p_position, float p_radius_px, const Style& p_style)
{
    const uint32_t radius = (uint32_t)(std::clamp(p_radius_px, 0.0f, POINT_RADIUS_MAX) * POINT_RADIUS_STEPS + 0.5f);
    std::lock_guard lock(mutex);
    _push(p_position, p_position, p_style.color, _flags(p_style) | FLAG_POINT | (radius << POINT_RADIUS_SHIFT), p_style.duration);
}

void DebugDraw::line(const vec3& p_a, const vec3& p_b, const Style& p_style)
{
    std::lock_guard lock(mutex);
    _push(p_a, p_b, p_style.color, _flags(p_style), p_style.duration);
}

void DebugDraw::box(const mat4& p_transform, const vec3& p_half_extents, const Style& p_style)
{
    vec3 c[8];
    for (uint32_t i = 0; i < 8; i++) {
        const vec3 local((i & 1) ? p_half_extents.x : -p_half_extents.x, (i & 2) ? p_half_extents.y : -p_half_extents.y, (i & 4) ? p_half_extents.z : -p_half_extents.z);
        c[i] = vec3(p_transform * vec4(local, 1.0f));
    }
    static constexpr uint8_t EDGES[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
    const uint32_t flags = _flags(p_style);
    std::lock_guard lock(mutex);
    for (const auto& e : EDGES) _push(c[e[0]], c[e[1]], p_style.color, flags, p_style.duration);
}

void DebugDraw::aabb(const vec3& p_min, const vec3& p_max, const Style& p_style)
{
    mat4 xf(1.0f);
    xf[3] = vec4((p_min + p_max) * 0.5f, 1.0f);
    box(xf, (p_max - p_min) * 0.5f, p_style);
}

void DebugDraw::circle(const vec3& p_center, const vec3& p_normal, float p_radius, const Style& p_style)
{
    vec3 u, v;
    _basis(normalize(p_normal), u, v);
    std::lock_guard lock(mutex);
    _arc(p_center, u, v, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
}

void DebugDraw::sphere(const vec3& p_center, float p_radius, const Style& p_style)
{
    std::lock_guard lock(mutex);
    const vec3 x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f), z(0.0f, 0.0f, 1.0f);
    _arc(p_center, x, z, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
    _arc(p_center, x, y, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
    _arc(p_center, z, y, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
}

void DebugDraw::capsule(const vec3& p_a, const vec3& p_b, float p_radius, const Style& p_style)
{
    const vec3 axis = p_b - p_a;
    const float len = length(axis);
    const vec3 d = len > 1e-6f ? axis / len : vec3(0.0f, 1.0f, 0.0f);
    vec3 u, v;
    _basis(d, u, v);
    const uint32_t half = CIRCLE_SEGMENTS / 2;
    const uint32_t flags = _flags(p_style);

    std::lock_guard lock(mutex);
    _arc(p_a, u, v, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
    _arc(p_b, u, v, p_radius, 0.0f, two_pi<float>(), CIRCLE_SEGMENTS, p_style);
    _arc(p_b, u, d, p_radius, 0.0f, pi<float>(), half, p_style);
    _arc(p_b, v, d, p_radius, 0.0f, pi<float>(), half, p_style);
    _arc(p_a, u, -d, p_radius, 0.0f, pi<float>(), half, p_style);
    _arc(p_a, v, -d, p_radius, 0.0f, pi<float>(), half, p_style);
    _push(p_a + u * p_radius, p_b + u * p_radius, p_style.color, flags, p_style.duration);
    _push(p_a - u * p_radius, p_b - u * p_radius, p_style.color, flags, p_style.duration);
    _push(p_a + v * p_radius, p_b + v * p_radius, p_style.color, flags, p_style.duration);
    _push(p_a - v * p_radius, p_b - v * p_radius, p_style.color, flags, p_style.duration);
}

void DebugDraw::arrow(const vec3& p_from, const vec3& p_to, const Style& p_style)
{
    const vec3 axis = p_to - p_from;
    const float len = length(axis);
    const uint32_t flags = _flags(p_style);
    std::lock_guard lock(mutex);
    _push(p_from, p_to, p_style.color, flags, p_style.duration);
    if (len < 1e-6f) return;

    const vec3 d = axis / len;
    vec3 u, v;
    _basis(d, u, v);
    const float head = len * 0.2f;
    const vec3 base = p_to - d * head;
    const float spread = head * 0.4f;
    _push(p_to, base + u * spread, p_style.color, flags, p_style.duration);
    _push(p_to, base - u * spread, p_style.color, flags, p_style.duration);
    _push(p_to, base + v * spread, p_style.color, flags, p_style.duration);
    _push(p_to, base - v * spread, p_style.color, flags, p_style.duration);
}

void DebugDraw::axes(const mat4& p_transform, float p_size, const Style& p_style)
{
    const vec3 o(p_transform[3]);
    const uint32_t flags = _flags(p_style);
    std::lock_guard lock(mutex);
    _push(o, o + normalize(vec3(p_transform[0])) * p_size, RED, flags, p_style.duration);
    _push(o, o + normalize(vec3(p_transform[1])) * p_size, GREEN, flags, p_style.duration);
    _push(o, o + normalize(vec3(p_transform[2])) * p_size, BLUE, flags, p_style.duration);
}

void DebugDraw::consume(std::vector<DebugLine>& r_out, float p_dt)
{
    std::lock_guard lock(mutex);
    r_out.clear();
    r_out.reserve(frame_lines.size() + timed_lines.size());
    r_out.insert(r_out.end(), frame_lines.begin(), frame_lines.end());
    r_out.insert(r_out.end(), timed_lines.begin(), timed_lines.end());
    frame_lines.clear();

    size_t i = 0;
    while (i < timed_lines.size()) {
        timed_left[i] -= p_dt;
        if (timed_left[i] > 0.0f) {
            i++;
            continue;
        }
        timed_lines[i] = timed_lines.back();
        timed_left[i] = timed_left.back();
        timed_lines.pop_back();
        timed_left.pop_back();
    }
}

void DebugDraw::clear()
{
    std::lock_guard lock(mutex);
    frame_lines.clear();
    timed_lines.clear();
    timed_left.clear();
}

}
