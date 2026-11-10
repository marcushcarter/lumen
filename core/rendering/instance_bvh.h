#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

struct InstanceBvh
{
    static constexpr uint32_t LEAF_SIZE = 4;
    static constexpr uint32_t NONE = UINT32_MAX;

    struct Node {
        vec3 min;
        uint32_t first;
        vec3 max;
        uint32_t count;
        uint32_t left;
        uint32_t right;
    };

    std::vector<Node> nodes;
    std::vector<uint32_t> order;

    void clear();
    void build(const std::vector<vec3>& p_min, const std::vector<vec3>& p_max);
    void cull(const vec4* p_planes, std::vector<uint32_t>& r_out) const;

    uint32_t _build(const std::vector<vec3>& p_min, const std::vector<vec3>& p_max, uint32_t p_first, uint32_t p_count);
};

}
