#pragma once
#include <core/rendering/features/feature.h>
#include <core/rendering/world_gpu.h>

namespace lumen {

struct GeometryFeature : Feature
{
    static constexpr uint32_t HIZ_TILE = 64;
    static constexpr uint32_t HIZ_TILE_MIPS = 7;
    static constexpr uint32_t HIZ_TAIL_MAX = 64;
    static constexpr uint32_t HIZ_MAX_MIPS = 16;
    static constexpr uint32_t VIS_EMPTY = 0xFFFFFFFFu;
    static constexpr uint32_t COMPACT_CMD_SIZE = 20;
    static constexpr uint32_t COMPACT_CMD_COUNT = 2 * RASTER_BIN_COUNT;
    static constexpr uint32_t COMPACT_ARGS_TRIS_OFFSET = COMPACT_CMD_COUNT * COMPACT_CMD_SIZE;
    static constexpr uint32_t COMPACT_ARGS_SPILL_OFFSET = COMPACT_ARGS_TRIS_OFFSET + COMPACT_CMD_COUNT * 4 + 2 * 4;
    static constexpr uint32_t COMPACT_ARGS_SIZE = COMPACT_ARGS_SPILL_OFFSET + 2 * 4;
    static constexpr uint32_t COMPACT_SPILL_CMD_SIZE = 16;
    static constexpr uint32_t COMPACT_MIN_TRIS = 256u * 1024;
    static constexpr uint32_t COMPACT_GRANULE_TRIS = 64u * 1024;
    static constexpr uint32_t COMPACT_SHRINK_FRAMES = 300;
    static constexpr uint64_t COMPACT_INDEX_LIMIT_TRIS = 0xFFFFFFFFull / 3ull;
    static constexpr uint32_t MAX_TRAVERSE_LEVELS = 16;
    static constexpr uint32_t SKIN_CACHE_VERTS = 512u * 1024;
    static constexpr uint32_t SKIN_CACHE_HEADER_SIZE = 64;
    static constexpr uint32_t REF_MIN = 64u * 1024;
    static constexpr uint32_t REF_GRANULE = 64u * 1024;
    static constexpr uint32_t REF_SHRINK_FRAMES = 300;

    struct RefFeedback {
        uint32_t count;
        uint32_t requested;
    };

    struct CullStats {
        uint32_t refs;
        uint32_t phase1_visible;
        uint32_t retest;
        uint32_t phase2_visible;
        uint32_t instances_visible;
        uint32_t instances_occluded;
        uint32_t instances_recovered;
    };

    struct CompactFeedback {
        uint32_t tris[COMPACT_CMD_COUNT];
        uint32_t done[2];
        uint32_t spill[2];
    };

    struct CompactRetired {
        uint64_t serial;
        drivers::DeviceDriverVulkan::Buffer buffer;
    };

    struct CullPush {
        VkDeviceAddress camera_addr;
        VkDeviceAddress geometry_addr;
        VkDeviceAddress instances_addr;
        VkDeviceAddress transforms_addr;
        VkDeviceAddress cluster_refs_addr;
        VkDeviceAddress list_a_addr;
        VkDeviceAddress list_b_addr;
        VkDeviceAddress skin_list_addr;
        uint32_t hiz_index;
        uint32_t hiz_mips;
        uint32_t hiz_enabled;
        uint32_t skin_enabled;
        float screen_size[2];
    };

    bool occlusion = true;
    bool occlusion_frame = true;
    bool contribution_culling = true;
    bool mesh_shading = true;
    bool task_shading = true;
    bool compact_raster_frame = true;
    bool skin_frame = false;
    uint32_t ref_capacity = 1;
    uint32_t ref_target = 0;
    uint32_t ref_demand = 0;
    uint32_t ref_low_frames = 0;
    std::vector<drivers::DeviceDriverVulkan::Buffer> ref_feedback;
    std::vector<uint8_t> ref_feedback_written;
    std::vector<uint32_t> ref_feedback_capacity;

    float contribution_px = 1.0f;
    bool hiz_history = false;
    bool hiz_use_prev = false;
    drivers::DeviceDriverVulkan::Sampler hiz_sampler;
    drivers::DeviceDriverVulkan::Sampler material_sampler;
    drivers::DeviceDriverVulkan::Buffer hiz_tail_buffer;
    bool hiz_ok = false;

    CullStats stats{};
    std::vector<drivers::DeviceDriverVulkan::Buffer> stats_readback;
    std::vector<uint8_t> stats_written;

    drivers::DeviceDriverVulkan::Buffer compact_indices;
    drivers::DeviceDriverVulkan::Buffer compact_prims;
    drivers::DeviceDriverVulkan::Buffer compact_materials;
    std::vector<CompactRetired> compact_retired;
    std::vector<drivers::DeviceDriverVulkan::Buffer> compact_feedback;
    std::vector<uint8_t> compact_feedback_written;
    uint64_t compact_serial = 0;
    uint32_t compact_capacity = 0;
    uint32_t compact_demand = 0;
    uint32_t compact_spilled = 0;
    uint32_t compact_resizes = 0;
    uint32_t compact_bin_capacity[RASTER_BIN_COUNT] = {};
    uint32_t compact_bin_base[RASTER_BIN_COUNT] = {};
    uint32_t compact_bin_demand[RASTER_BIN_COUNT] = {};
    uint32_t compact_low_frames[RASTER_BIN_COUNT] = {};
    uint64_t compact_failed_tris = 0;

    RenderGraph::Pass clear_visible_pass;
    RenderGraph::Pass instance_cull_pass;
    RenderGraph::Pass node_seed_pass;
    RenderGraph::Pass cluster_cull_args_pass;
    RenderGraph::Pass cluster_cull_pass;
    RenderGraph::Pass skin_pass;
    RenderGraph::Pass skin_pass_2;
    RenderGraph::Pass hiz_build_pass;
    RenderGraph::Pass instance_cull_pass_2;
    RenderGraph::Pass node_seed_pass_2;
    RenderGraph::Pass node_traverse_passes[2][MAX_TRAVERSE_LEVELS];
    RenderGraph::Pass cluster_retest_args_pass;
    RenderGraph::Pass cluster_retest_pass;
    RenderGraph::Pass hiz_build_pass_2;
    RenderGraph::Pass draw_compact_pass;
    RenderGraph::Pass draw_compact_pass_2;
    RenderGraph::Pass visbuffer_compact_pass;
    RenderGraph::Pass visbuffer_compact_pass_2;
    RenderGraph::Pass compact_feedback_pass;
    RenderGraph::Pass material_resolve_pass;
    RenderGraph::Pass stats_pass;
    RenderGraph::Pass ref_feedback_pass;

    drivers::DeviceDriverVulkan::Pipeline instance_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline node_seed_pipe;
    drivers::DeviceDriverVulkan::Pipeline node_traverse_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_compact_pipe;
    drivers::DeviceDriverVulkan::Pipeline visbuffer_compact_pipes[RASTER_BIN_COUNT];
    drivers::DeviceDriverVulkan::Pipeline visbuffer_spill_pipes[RASTER_BIN_COUNT];
    drivers::DeviceDriverVulkan::Pipeline material_resolve_pipe;
    drivers::DeviceDriverVulkan::Pipeline skin_clusters_pipe;

    void _compact_update(bool p_feedback, const uint64_t* p_demand);
    bool _compact_reserve(const uint32_t* p_bin_tris);
    void _compact_retire(drivers::DeviceDriverVulkan::Buffer& r_buffer);
    void _compact_collect(bool p_all);

    void _create_clear_visible_pass();
    void _create_instance_cull_pass(RenderGraph::Pass& r_pass, bool p_late);
    uint32_t _node_queue_capacity() const;
    void _create_node_seed_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_node_traverse_pass(RenderGraph::Pass& r_pass, bool p_late, uint32_t p_level);
    void _create_cluster_cull_args_pass();
    void _create_cluster_cull_pass();
    void _create_skin_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_hiz_pass(RenderGraph::Pass& r_build, const char* p_build_name, const char* p_args);
    void _create_cluster_retest_args_pass();
    void _create_cluster_retest_pass();
    void _create_draw_compact_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_visbuffer_compact_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_compact_feedback_pass();
    void _create_material_resolve_pass();
    void _create_stats_pass();
    void _create_ref_feedback_pass();
    void _ref_update(uint32_t p_slot);

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
