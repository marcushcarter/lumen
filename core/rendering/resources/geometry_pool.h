#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <filesystem>

namespace lumen {

using namespace glm;

struct GeometryAddresses {
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
    VkDeviceAddress tri_slots;
    VkDeviceAddress slot_table;
    VkDeviceAddress clusters;
    VkDeviceAddress skin_vertices;
    VkDeviceAddress bvh_nodes;
    VkDeviceAddress meshes;
};

struct LMesh {
    uint32_t vertex_base;
    uint32_t vertex_count;
    uint32_t cluster_base;
    uint32_t cluster_count;
    uint32_t slot_table_base;
    uint32_t slot_table_count;
    uint32_t skin_base;
    uint32_t bvh_node_base;
    uint32_t bvh_node_count;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

struct GeometryPool
{
    static constexpr uint32_t MAX_VERTS = 32u * 1024 * 1024;
    static constexpr uint32_t MAX_INDICES = 96u * 1024 * 1024;
    static constexpr uint32_t MAX_TRI_SLOTS = MAX_INDICES / 3;
    static constexpr uint32_t MAX_SLOT_TABLE = 256u * 1024;
    static constexpr uint32_t MAX_CLUSTERS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_SKIN_VERTS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_BVH_NODES = 8u * 1024 * 1024;
    static constexpr uint32_t MAX_MESHES = 64u * 1024;

    drivers::DeviceDriverVulkan* dd = nullptr;

    drivers::DeviceDriverVulkan::Buffer vertex_buffer;
    drivers::DeviceDriverVulkan::Buffer index_buffer;
    drivers::DeviceDriverVulkan::Buffer tri_slot_buffer;
    drivers::DeviceDriverVulkan::Buffer slot_table_buffer;
    drivers::DeviceDriverVulkan::Buffer cluster_buffer;
    drivers::DeviceDriverVulkan::Buffer skin_buffer;
    drivers::DeviceDriverVulkan::Buffer bvh_nodes_buffer;
    drivers::DeviceDriverVulkan::Buffer mesh_buffer;
    drivers::DeviceDriverVulkan::Buffer address_buffer;
    
    uint32_t vertex_head = 0;
    uint32_t index_head = 0;
    uint32_t slot_table_head = 0;
    uint32_t cluster_head = 0;
    uint32_t skin_head = 0;
    uint32_t bvhn_head = 0;
    bool allocated = false;

    std::vector<LMesh> meshes;
    std::vector<Guid> mesh_guids;
    std::vector<uint32_t> free_meshes;
    std::unordered_map<Guid, uint32_t, GuidHash, GuidEq> by_guid;

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    Error allocate();
    void free();
    
    uint32_t load(Guid p_guid, const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    void clear();

    // const LMesh* get(uint32_t p_mesh_id) const {
    //     auto it = by_guid.find(p_mesh_id);
    //     return it == by_guid.end() ? nullptr : &slots[it->second];
    // }

    // Guid guid_of(uint32_t p_mesh_id) const {
    //     auto it = by_guid.find(p_guid);
    //     return it == by_guid.end() ? nullptr : &slots[it->second];
    // }

    // const LMesh* get(Guid p_guid) const {
    //     auto it = by_guid.find(p_guid);
    //     return it == by_guid.end() ? nullptr : &slots[it->second];
    // }
};

}