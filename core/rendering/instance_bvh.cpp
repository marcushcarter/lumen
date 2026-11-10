#include <core/rendering/instance_bvh.h>
#include <algorithm>
#include <cfloat>

namespace lumen {

static constexpr uint32_t PLANE_COUNT = 6;
static constexpr uint32_t ALL_PLANES = (1u << PLANE_COUNT) - 1u;
static constexpr uint32_t STACK_SIZE = 64;

void InstanceBvh::clear()
{
    nodes.clear();
    order.clear();
}

void InstanceBvh::build(const std::vector<vec3>& p_min, const std::vector<vec3>& p_max)
{
    clear();
    const uint32_t count = (uint32_t)p_min.size();
    if (count == 0) return;
    order.resize(count);
    for (uint32_t i = 0; i < count; i++) order[i] = i;
    nodes.reserve((size_t)count * 2 / LEAF_SIZE + 1);
    _build(p_min, p_max, 0, count);
}

uint32_t InstanceBvh::_build(const std::vector<vec3>& p_min, const std::vector<vec3>& p_max, uint32_t p_first, uint32_t p_count)
{
    const uint32_t index = (uint32_t)nodes.size();
    nodes.push_back({ vec3(FLT_MAX), p_first, vec3(-FLT_MAX), p_count, NONE, NONE });

    vec3 lo(FLT_MAX), hi(-FLT_MAX), c_lo(FLT_MAX), c_hi(-FLT_MAX);
    for (uint32_t i = p_first; i < p_first + p_count; i++) {
        const uint32_t k = order[i];
        lo = glm::min(lo, p_min[k]);
        hi = glm::max(hi, p_max[k]);
        const vec3 c = (p_min[k] + p_max[k]) * 0.5f;
        c_lo = glm::min(c_lo, c);
        c_hi = glm::max(c_hi, c);
    }
    nodes[index].min = lo;
    nodes[index].max = hi;
    if (p_count <= LEAF_SIZE) return index;

    const vec3 extent = c_hi - c_lo;
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const uint32_t half = p_count / 2;
    std::nth_element(order.begin() + p_first, order.begin() + p_first + half, order.begin() + p_first + p_count, [&](uint32_t a, uint32_t b) {
        return p_min[a][axis] + p_max[a][axis] < p_min[b][axis] + p_max[b][axis];
    });

    const uint32_t left = _build(p_min, p_max, p_first, half);
    const uint32_t right = _build(p_min, p_max, p_first + half, p_count - half);
    nodes[index].left = left;
    nodes[index].right = right;
    return index;
}

void InstanceBvh::cull(const vec4* p_planes, std::vector<uint32_t>& r_out) const
{
    if (nodes.empty()) return;
    struct Item {
        uint32_t node;
        uint32_t planes;
    };
    Item stack[STACK_SIZE];
    uint32_t top = 0;
    stack[top++] = { 0u, ALL_PLANES };
    while (top > 0) {
        const Item item = stack[--top];
        const Node& n = nodes[item.node];
        const vec3 center = (n.min + n.max) * 0.5f;
        const vec3 half = (n.max - n.min) * 0.5f;
        uint32_t planes = item.planes;
        bool outside = false;
        for (uint32_t p = 0; p < PLANE_COUNT && !outside; p++) {
            if ((planes & (1u << p)) == 0) continue;
            const vec3 normal = vec3(p_planes[p]);
            const float d = dot(normal, center) + p_planes[p].w;
            const float r = dot(abs(normal), half);
            if (d < -r) outside = true;
            else if (d >= r) planes &= ~(1u << p);
        }
        if (outside) continue;
        if (planes == 0 || n.left == NONE || top + 2 > STACK_SIZE) {
            r_out.insert(r_out.end(), order.begin() + n.first, order.begin() + n.first + n.count);
            continue;
        }
        stack[top++] = { n.right, planes };
        stack[top++] = { n.left, planes };
    }
}

}
