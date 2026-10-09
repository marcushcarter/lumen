#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <core/base/range_allocator.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace lumen {

using namespace glm;

struct GeometryAddresses
{
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
    VkDeviceAddress tri_slots;
    VkDeviceAddress slot_table;
    VkDeviceAddress clusters;
    VkDeviceAddress groups;
    VkDeviceAddress skin_vertices;
    VkDeviceAddress bvh_nodes;
    VkDeviceAddress meshes;
};

struct LMesh
{
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
    static constexpr uint32_t MAX_TRIS = 32u * 1024 * 1024;
    static constexpr uint32_t MAX_SLOT_TABLE = 256u * 1024;
    static constexpr uint32_t MAX_CLUSTERS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_GROUPS = 512u * 1024;
    static constexpr uint32_t MAX_SKIN_VERTS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_BVH_NODES = 8u * 1024 * 1024;
    static constexpr uint32_t MAX_MESHES = 64u * 1024;
    static constexpr uint32_t INVALID_MESH = UINT32_MAX;
    
    /***************/
    /**** SETUP ****/
    /***************/

    drivers::DeviceDriverVulkan* dd = nullptr;
    uint32_t frame_count = 1;
    uint64_t frame_number = 0;
    bool allocated = false;
    
    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();

    Error allocate();
    void free();
    void clear();
    
    /****************/
    /**** ARENAS ****/
    /****************/

    enum ArenaKind : uint32_t {
        ARENA_VERTICES,
        ARENA_INDICES,
        ARENA_TRI_SLOTS,
        ARENA_SLOT_TABLE,
        ARENA_CLUSTERS,
        ARENA_GROUPS,
        ARENA_SKIN,
        ARENA_BVH_NODES,
        ARENA_MESHES,
        ARENA_COUNT,
    };

    struct Arena {
        drivers::DeviceDriverVulkan::Buffer buffer;
        uint32_t capacity = 0;
        uint32_t stride = 0;
        uint32_t initial = 0;
        uint32_t max = 0;
        VkBufferUsageFlags usage = 0;
        const char* name = nullptr;
    };

    struct MeshRanges {
        uint32_t vertex = RangeAllocator::INVALID, vertex_count = 0;
        uint32_t tri = RangeAllocator::INVALID, tri_count = 0;
        uint32_t slot_table = RangeAllocator::INVALID, slot_table_count = 0;
        uint32_t cluster = RangeAllocator::INVALID, cluster_count = 0;
        uint32_t group = RangeAllocator::INVALID, group_count = 0;
        uint32_t skin = RangeAllocator::INVALID, skin_count = 0;
        uint32_t bvh = RangeAllocator::INVALID, bvh_count = 0;
    };

    Arena arenas[ARENA_COUNT];
    std::vector<drivers::DeviceDriverVulkan::Buffer> address_buffers;

    RangeAllocator vertex_alloc;
    RangeAllocator tri_alloc;
    RangeAllocator slot_table_alloc;
    RangeAllocator cluster_alloc;
    RangeAllocator group_alloc;
    RangeAllocator skin_alloc;
    RangeAllocator bvh_alloc;
    
    uint32_t cluster_extent = 0;
    
    void _arena_setup(ArenaKind p_kind, uint32_t p_stride, uint32_t p_initial, uint32_t p_max, VkBufferUsageFlags p_usage, const char* p_name);
    Error _arena_create(Arena& r_arena, uint32_t p_capacity);
    uint32_t _grow_target(ArenaKind p_kind, uint64_t p_min) const;
    Error _arena_grow(ArenaKind p_kind, uint32_t p_capacity, uint32_t p_live);
    uint32_t _allocate(RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count);
    void _release(const MeshRanges& p_ranges);
    void _retire_buffer(drivers::DeviceDriverVulkan::Buffer& r_buffer);
    void _collect(uint64_t p_frame_number);

    /****************/
    /**** MESHES ****/
    /****************/

    struct RetiredMesh {
        uint64_t frame;
        uint32_t id;
        MeshRanges ranges;
    };

    struct RetiredBuffer {
        uint64_t frame;
        drivers::DeviceDriverVulkan::Buffer buffer;
    };

    std::vector<LMesh> meshes;
    std::vector<MeshRanges> mesh_ranges;
    std::vector<Guid> mesh_guids;
    std::vector<uint32_t> free_meshes;
    std::unordered_map<Guid, uint32_t, GuidHash, GuidEq> by_guid;

    uint64_t version = 0;

    std::vector<RetiredMesh> retired_meshes;
    std::vector<RetiredBuffer> retired_buffers;

    uint32_t load(Guid p_guid, const std::filesystem::path& p_path);
    void unload(Guid p_guid);

    uint32_t find(Guid p_guid) const { auto it = by_guid.find(p_guid); return it == by_guid.end() ? INVALID_MESH : it->second; }
    const LMesh* get(uint32_t p_id) const { return p_id < meshes.size() && mesh_guids[p_id] != Guid{} ? &meshes[p_id] : nullptr; }

    /***************/
    /**** FRAME ****/
    /***************/

    void begin_frame(uint64_t p_frame_number, uint32_t p_slot);

    drivers::DeviceDriverVulkan::Buffer& address_buffer(uint32_t p_slot) { return address_buffers[p_slot]; }
    const drivers::DeviceDriverVulkan::Buffer& index_buffer() const { return arenas[ARENA_INDICES].buffer; }
    VkDeviceSize resident_bytes() const;
};

}