#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <core/base/range_allocator.h>
#include <core/assets/lmesh.h>
#include <core/rendering/resources/mesh_page_reader.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <cfloat>

namespace lumen {

using namespace glm;

struct GeometryAddresses
{
    VkDeviceAddress tri_slots;
    VkDeviceAddress slot_table;
    VkDeviceAddress clusters;
    VkDeviceAddress groups;
    VkDeviceAddress skin_vertices;
    VkDeviceAddress meshes;
    VkDeviceAddress cluster_vertex_base;
    VkDeviceAddress cluster_skin_delta;
    VkDeviceAddress local_triangles;
    VkDeviceAddress cluster_vertices;
    VkDeviceAddress materials;
    VkDeviceAddress cluster_slots;
    VkDeviceAddress cluster_bounds;
    VkDeviceAddress nodes;
    VkDeviceAddress page_resident;
    VkDeviceAddress page_stamp;
    VkDeviceAddress group_refined;
    VkDeviceAddress stream_requests;
    uint32_t stream_frame;
    uint32_t stream_request_capacity;
};

struct LMesh
{
    uint32_t cluster_base;
    uint32_t cluster_count;
    uint32_t slot_count;
    uint32_t node_base;
    uint32_t node_count;
    uint32_t page_base;
    uint32_t page_count;
    uint32_t flags;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

struct GeometryPool
{
    static constexpr uint32_t MAX_TRIS = 32u * 1024 * 1024;
    static constexpr uint32_t MAX_CLUSTERS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_CLUSTER_VERTS = 64u * 1024 * 1024;
    static constexpr uint32_t RASTER_CLUSTER_TRIS = LMESH_CLUSTER_MAX_TRIS;
    static constexpr uint32_t RASTER_CLUSTER_VERTS = LMESH_CLUSTER_MAX_VERTS;
    static constexpr uint32_t MAX_GROUPS = 512u * 1024;
    static constexpr uint32_t MAX_SKIN_VERTS = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_MESHES = 64u * 1024;
    static constexpr uint32_t MAX_NODES = 8u * 1024 * 1024;
    static constexpr uint32_t MAX_PAGES = 4u * 1024 * 1024;
    static constexpr uint32_t MAX_NODE_DEPTH = 16;
    static constexpr uint32_t LOCAL_TRI_SLOT_OVERFLOW = LMESH_LOCAL_SLOT_OVERFLOW;
    static constexpr uint32_t CLUSTER_SLOT_MIXED = LMESH_CLUSTER_SLOT_MIXED;
    static constexpr uint32_t INVALID_MESH = UINT32_MAX;
    static constexpr uint32_t MESH_FLAG_STREAMED = 1u;

    static constexpr uint32_t STREAM_REQUEST_CAPACITY = 64u * 1024;
    static constexpr uint32_t STREAM_REQUEST_RESIDENT_BIT = 0x80000000u;
    static constexpr VkDeviceSize STREAM_STAGING_BYTES = 16ull * 1024 * 1024;
    static constexpr uint32_t STREAM_MAX_IN_FLIGHT = 256;
    static constexpr uint32_t STREAM_MAX_EVICT_PER_FRAME = 256;
    static constexpr uint64_t STREAM_EVICT_AGE_FRAMES = 120;
    static constexpr uint64_t STREAM_DEFAULT_BUDGET = 512ull * 1024 * 1024;
    static constexpr uint64_t STREAM_EVICT_RESCAN_FRAMES = 15;

    enum class ArenaKind : uint32_t {
        TRI_SLOTS,
        CLUSTERS,
        GROUPS,
        SKIN,
        MESHES,
        CLUSTER_VERTEX_BASE,
        LOCAL_TRIANGLES,
        CLUSTER_VERTICES,
        CLUSTER_SLOTS,
        CLUSTER_BOUNDS,
        NODES,
        PAGE_RESIDENT,
        PAGE_STAMP,
        GROUP_REFINED,
        CLUSTER_SKIN_DELTA,
        COUNT,
    };

    static constexpr uint32_t ARENA_COUNT = (uint32_t)ArenaKind::COUNT;

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
        uint32_t cluster = RangeAllocator::INVALID, cluster_count = 0;
        uint32_t group = RangeAllocator::INVALID, group_count = 0;
        uint32_t skin = RangeAllocator::INVALID, skin_count = 0;
        uint32_t node = RangeAllocator::INVALID, node_count = 0;
        uint32_t page = RangeAllocator::INVALID, page_count = 0;
    };

    struct JointBox {
        vec3 min = vec3(FLT_MAX);
        vec3 max = vec3(-FLT_MAX);
    };

    struct RetiredMesh {
        uint64_t frame;
        uint32_t id;
        MeshRanges ranges;
    };

    struct RetiredPage {
        uint64_t frame;
        uint32_t tri, tri_count;
        uint32_t vertex, vertex_count;
        uint32_t cluster, cluster_count;
        uint32_t skin, skin_count;
    };

    struct RetiredBuffer {
        uint64_t frame;
        drivers::DeviceDriverVulkan::Buffer buffer;
    };

    enum class PageState : uint8_t { ABSENT, LOADING, RESIDENT, FAILED };

    struct StreamPage {
        uint32_t tri = RangeAllocator::INVALID;
        uint32_t vertex = RangeAllocator::INVALID;
        uint32_t cluster = RangeAllocator::INVALID;
        uint32_t skin = RangeAllocator::INVALID;
        uint32_t list_index = UINT32_MAX;
        uint64_t last_used = 0;
        PageState state = PageState::ABSENT;
    };

    struct StreamMesh {
        bool streamed = false;
        bool blocked = false;
        uint32_t generation = 0;
        uint64_t required_until = 0;
        uint32_t resident_pages = 0;
        uint32_t loading_pages = 0;
        uint32_t failed_pages = 0;
        std::shared_ptr<MeshPageReader::Source> source;
        std::vector<StreamPage> pages;
        std::vector<uint32_t> page_groups_first;
        std::vector<uint32_t> page_groups;
        std::vector<uint32_t> group_missing;
        std::vector<uint32_t> page_leaves_first;
        std::vector<uint32_t> page_leaves;
    };

    struct PendingGrowth {
        drivers::DeviceDriverVulkan::Buffer src;
        drivers::DeviceDriverVulkan::Buffer dst;
        VkDeviceSize bytes;
    };

    struct StagedCopy {
        ArenaKind kind;
        VkDeviceSize src;
        VkDeviceSize dst;
        VkDeviceSize size;
    };

    struct StagingSlot {
        drivers::DeviceDriverVulkan::Buffer buffer;
        VkDeviceSize used = 0;
        std::vector<StagedCopy> copies;
    };

    struct Wanted {
        uint32_t mesh;
        uint32_t page;
        float priority;
    };

    struct StreamStats {
        uint32_t requests = 0;
        uint32_t loads = 0;
        uint32_t evictions = 0;
        uint32_t in_flight = 0;
        uint32_t resident_pages = 0;
        uint32_t streamed_pages = 0;
        uint32_t failed_pages = 0;
        uint64_t resident_bytes = 0;
        uint64_t budget_bytes = 0;
        uint64_t uploaded_bytes = 0;
    };

    drivers::DeviceDriverVulkan* dd = nullptr;
    uint32_t frame_count = 1;
    uint64_t frame_number = 0;
    uint32_t current_slot = 0;
    uint64_t version = 0;

    Arena arenas[ARENA_COUNT];
    std::vector<drivers::DeviceDriverVulkan::Buffer> address_buffers;

    RangeAllocator vertex_alloc;
    RangeAllocator tri_alloc;
    RangeAllocator cluster_alloc;
    RangeAllocator group_alloc;
    RangeAllocator skin_alloc;
    RangeAllocator node_alloc;
    RangeAllocator page_alloc;
    uint32_t cluster_extent = 0;
    uint32_t max_cluster_tris = 0;
    uint32_t max_node_depth = 0;

    std::vector<LMesh> meshes;
    std::vector<MeshRanges> mesh_ranges;
    std::vector<Guid> mesh_guids;
    std::vector<std::vector<JointBox>> mesh_joint_boxes;
    std::vector<LMeshRig> mesh_rigs;

    std::vector<uint32_t> free_meshes;
    std::vector<RetiredMesh> _retired_meshes;
    std::vector<RetiredPage> _retired_pages;
    std::vector<RetiredBuffer> _retired_buffers;
    std::unordered_map<Guid, uint32_t> by_guid;

    uint64_t stream_budget = STREAM_DEFAULT_BUDGET;
    uint64_t _stream_resident_bytes = 0;
    uint64_t _stream_loading_bytes = 0;
    uint32_t _stream_generation = 0;
    uint32_t _stream_total_pages = 0;
    uint32_t _stream_resident_pages = 0;
    uint32_t _stream_failed_pages = 0;
    uint64_t _evict_scan_after = 0;
    bool _deferred_growth = false;
    std::vector<PendingGrowth> _pending_growth;
    std::vector<uint32_t> _blocked;
    std::vector<StreamMesh> streams;
    std::vector<uint32_t> page_owner;
    std::vector<uint64_t> _resident_list;
    std::vector<uint32_t> _required;
    std::vector<MeshPageReader::Result> _results;
    std::vector<Wanted> _wanted;
    MeshPageReader reader;
    StreamStats stream_stats;

    drivers::DeviceDriverVulkan::Buffer stream_requests;
    std::vector<drivers::DeviceDriverVulkan::Buffer> request_readback;
    std::vector<uint8_t> request_written;
    std::vector<StagingSlot> staging;

    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();
    void begin_frame(uint64_t p_frame_number, uint32_t p_slot, VkDeviceAddress p_slot_table, VkDeviceAddress p_materials);
    void record_begin(VkCommandBuffer p_cmd);
    void record_end(VkCommandBuffer p_cmd);

    Arena& _arena(ArenaKind p_kind) { return arenas[(uint32_t)p_kind]; }
    const Arena& _arena(ArenaKind p_kind) const { return arenas[(uint32_t)p_kind]; }
    void _arena_setup(ArenaKind p_kind, uint32_t p_stride, uint32_t p_initial, uint32_t p_max, VkBufferUsageFlags p_usage, const char* p_name);
    Error _arena_create(Arena& r_arena, uint32_t p_capacity);
    uint32_t _grow_target(ArenaKind p_kind, uint64_t p_min) const;
    Error _arena_grow(ArenaKind p_kind, uint32_t p_capacity, uint32_t p_live);
    void _flush_growth();
    bool _ensure_tri_slots(uint32_t p_end);
    uint32_t _allocate(RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count);
    void _release(const MeshRanges& p_ranges);
    void _retire_buffer(drivers::DeviceDriverVulkan::Buffer& r_buffer);
    void _collect(uint64_t p_frame_number);
    VkDeviceSize _resident_bytes() const;

    uint32_t _load_resident(Guid p_guid, const std::string& p_name, LMeshData& r_data, uint32_t p_id);
    uint32_t _load_streamed(Guid p_guid, const std::filesystem::path& p_path, LMeshData& r_tables, uint32_t p_id);
    uint32_t _claim_id();
    uint32_t _commit_id(uint32_t p_id, Guid p_guid, const LMesh& p_mesh, const MeshRanges& p_ranges, std::vector<JointBox>&& r_boxes, LMeshRig&& r_rig);
    bool _hierarchy_depth(std::vector<LMeshNode>& r_nodes, const MeshRanges& p_ranges, uint32_t& r_depth) const;

    bool _stage(ArenaKind p_kind, uint32_t p_offset, const void* p_data, VkDeviceSize p_bytes);
    bool _stage_bytes(ArenaKind p_kind, VkDeviceSize p_dst, const void* p_data, VkDeviceSize p_bytes);
    void _page_clusters(const LMeshData& p_tables, uint32_t p_page, const StreamPage& p_slot, uint32_t p_group_base, std::vector<Cluster>& r_clusters, std::vector<uint32_t>& r_vbase, std::vector<uint32_t>& r_skin, std::vector<uint32_t>& r_slots, std::vector<uint32_t>& r_bounds) const;
    void _retire_page(StreamPage& r_page, const LMeshPage& p_info);
    VkDeviceSize _staging_free() const;
    static uint64_t _page_gpu_bytes(const LMeshPage& p_page, bool p_wide, bool p_skinned);
    static std::vector<JointBox> _joint_boxes(const std::vector<LMeshJointBox>& p_boxes);
    void _stream_tick();
    void _stream_read_requests();
    void _stream_take_results();
    void _stream_issue_loads();
    bool _page_install(uint32_t p_mesh, uint32_t p_page, const LMeshPageData& p_data);
    void _page_evict(uint32_t p_mesh, uint32_t p_page);
    bool _evict_for(uint64_t p_bytes, uint32_t& r_budget_evictions, std::vector<uint64_t>& r_candidates, size_t& r_cursor, bool& r_built);
    void _stream_forget(uint32_t p_mesh);
    void _stream_evict_all();
    void _register_commands();

    uint32_t load(Guid p_guid, const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    void clear();
    void free();

    void require(uint32_t p_id, uint32_t p_frames = 8);
    bool fully_resident(uint32_t p_id) const;
    bool settled(uint32_t p_id) const;

    drivers::DeviceDriverVulkan::Buffer& address_buffer(uint32_t p_slot) { return address_buffers[p_slot]; }

    uint32_t find(Guid p_guid) const {
        auto it = by_guid.find(p_guid);
        return it == by_guid.end() ? INVALID_MESH : it->second;
    }

    const LMesh* get(uint32_t p_id) const {
        return p_id < meshes.size() && mesh_guids[p_id].valid() ? &meshes[p_id] : nullptr;
    }

    const LMeshRig* rig(uint32_t p_id) const {
        return get(p_id) && !mesh_rigs[p_id].joints.empty() ? &mesh_rigs[p_id] : nullptr;
    }

    void set_skeleton(Guid p_mesh, Guid p_skeleton) {
        const uint32_t id = find(p_mesh);
        if (!get(id)) return;
        mesh_rigs[id].skeleton = p_skeleton;
        version++;
    }

    void posed_box(uint32_t p_id, const mat4* p_skin, uint32_t p_count, vec3& r_min, vec3& r_max) const;

    const std::vector<JointBox>* joint_boxes(uint32_t p_id) const {
        return get(p_id) && !mesh_joint_boxes[p_id].empty() ? &mesh_joint_boxes[p_id] : nullptr;
    }
};

}
