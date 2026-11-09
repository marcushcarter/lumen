#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <vector>

namespace lumen {

using namespace glm;

enum class DebugCategory : uint32_t
{
    GENERAL = 1u << 0,
    COLLISION = 1u << 1,
    BONES = 1u << 2,
};

struct DebugLine
{
    vec3 a;
    uint32_t color;
    vec3 b;
    uint32_t flags;
};

static_assert(sizeof(DebugLine) == 32, "DebugLine must match the GPU line layout");
static_assert(std::is_trivially_copyable_v<DebugLine>, "must be blittable");

constexpr uint32_t debug_rgb(uint32_t p_hex, uint32_t p_alpha = 0xFF)
{
    return ((p_hex >> 16) & 0xFF) | (p_hex & 0xFF00) | ((p_hex & 0xFF) << 16) | (p_alpha << 24);
}

struct DebugDraw
{
    static constexpr uint32_t CATEGORY_MASK = 0xFFu;
    static constexpr uint32_t FLAG_XRAY = 1u << 8;
    static constexpr uint32_t MAX_LINES = 1u << 20;
    static constexpr uint32_t CIRCLE_SEGMENTS = 32;

    static constexpr uint32_t WHITE = debug_rgb(0xFFFFFF);
    static constexpr uint32_t RED = debug_rgb(0xFF3352);
    static constexpr uint32_t GREEN = debug_rgb(0x8BDC00);
    static constexpr uint32_t BLUE = debug_rgb(0x2890FF);
    static constexpr uint32_t YELLOW = debug_rgb(0xFFD200);
    static constexpr uint32_t ORANGE = debug_rgb(0xFF8A2A);
    static constexpr uint32_t CYAN = debug_rgb(0x38E0E0);

    struct Style {
        uint32_t color = WHITE;
        DebugCategory category = DebugCategory::GENERAL;
        bool xray = false;
        float duration = 0.0f;
    };

    std::mutex mutex;
    std::vector<DebugLine> frame_lines;
    std::vector<DebugLine> timed_lines;
    std::vector<float> timed_left;

    static uint32_t _flags(const Style& p_style);
    void _push(const vec3& p_a, const vec3& p_b, uint32_t p_color, uint32_t p_flags, float p_duration);
    void _arc(const vec3& p_center, const vec3& p_x, const vec3& p_y, float p_radius, float p_from, float p_to, uint32_t p_segments, const Style& p_style);

    void line(const vec3& p_a, const vec3& p_b, const Style& p_style = {});
    void box(const mat4& p_transform, const vec3& p_half_extents, const Style& p_style = {});
    void aabb(const vec3& p_min, const vec3& p_max, const Style& p_style = {});
    void circle(const vec3& p_center, const vec3& p_normal, float p_radius, const Style& p_style = {});
    void sphere(const vec3& p_center, float p_radius, const Style& p_style = {});
    void capsule(const vec3& p_a, const vec3& p_b, float p_radius, const Style& p_style = {});
    void arrow(const vec3& p_from, const vec3& p_to, const Style& p_style = {});
    void axes(const mat4& p_transform, float p_size, const Style& p_style = {});

    void consume(std::vector<DebugLine>& r_out, float p_dt);
    void clear();
};

DebugDraw& debug_draw();

}
