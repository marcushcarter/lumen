#pragma once
#include <core/rendering/features/feature.h>

namespace lumen {

struct GeometryFeature : Feature
{
    /******************/
    /**** CLUSTERS ****/
    /******************/

    static constexpr uint32_t HIZ_TILE = 64;
    static constexpr uint32_t HIZ_TILE_MIPS = 7;
    static constexpr uint32_t HIZ_TAIL_MAX = 64;
    static constexpr uint32_t HIZ_MAX_MIPS = 16;
    static constexpr uint32_t VIS_EMPTY = 0xFFFFFFFFu;
    static constexpr uint32_t COMPACT_ARGS_SIZE = 64;
    static constexpr uint32_t COMPACT_ARGS_TRIS_OFFSET = 40;
    static constexpr uint32_t COMPACT_ARGS_SPILL_OFFSET = 56;
    static constexpr uint32_t COMPACT_SPILL_CMD_SIZE = 16;
    static constexpr uint32_t COMPACT_MIN_TRIS = 256u * 1024;
    static constexpr uint32_t COMPACT_GRANULE_TRIS = 64u * 1024;
    static constexpr uint32_t COMPACT_SHRINK_FRAMES = 300;
    static constexpr uint64_t COMPACT_INDEX_LIMIT_TRIS = 0xFFFFFFFFull / 3ull;

    struct CullPush {
        VkDeviceAddress camera_addr;
        VkDeviceAddress geometry_addr;
        VkDeviceAddress instances_addr;
        VkDeviceAddress transforms_addr;
        VkDeviceAddress cluster_refs_addr;
        VkDeviceAddress list_a_addr;
        VkDeviceAddress list_b_addr;
        uint32_t hiz_index;
        uint32_t hiz_mips;
        uint32_t hiz_enabled;
        uint32_t _pad;
        float screen_size[2];
    };

    bool occlusion = true;
    bool occlusion_frame = true;
    bool contribution_culling = true;
    bool mesh_shading = true;
    bool task_shading = true;
    bool compact_raster_frame = true;

    float contribution_px = 1.0f;
    bool hiz_history = false;
    bool hiz_use_prev = false;
    drivers::DeviceDriverVulkan::Sampler hiz_sampler;
    drivers::DeviceDriverVulkan::Buffer hiz_tail_buffer;
    bool hiz_ok = false;

    RenderGraph::Pass clear_visible_pass;
    RenderGraph::Pass instance_cull_pass;
    RenderGraph::Pass cluster_expand_args_pass;
    RenderGraph::Pass cluster_expand_pass;
    RenderGraph::Pass cluster_cull_args_pass;
    RenderGraph::Pass cluster_cull_pass;
    RenderGraph::Pass hiz_build_pass;
    RenderGraph::Pass instance_cull_pass_2;
    RenderGraph::Pass cluster_expand_args_pass_2;
    RenderGraph::Pass cluster_expand_pass_2;
    RenderGraph::Pass cluster_retest_args_pass;
    RenderGraph::Pass cluster_retest_pass;
    RenderGraph::Pass hiz_build_pass_2;
    RenderGraph::Pass draw_compact_pass;
    RenderGraph::Pass draw_compact_pass_2;
    RenderGraph::Pass visbuffer_compact_pass;
    RenderGraph::Pass visbuffer_compact_pass_2;

    drivers::DeviceDriverVulkan::Pipeline instance_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_expand_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_expand_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_compact_pipe;
    drivers::DeviceDriverVulkan::Pipeline visbuffer_compact_pipe;
    drivers::DeviceDriverVulkan::Pipeline visbuffer_spill_pipe;

    struct CompactFeedback {
        uint32_t tris[2];
        uint32_t done[2];
        uint32_t spill[2];
    };

    struct CompactRetired {
        drivers::DeviceDriverVulkan::Buffer buffer;
        uint64_t serial;
    };

    drivers::DeviceDriverVulkan::Buffer compact_indices;
    drivers::DeviceDriverVulkan::Buffer compact_prims;
    std::vector<CompactRetired> compact_retired;
    std::vector<drivers::DeviceDriverVulkan::Buffer> compact_feedback;
    std::vector<uint8_t> compact_feedback_written;
    uint64_t compact_serial = 0;
    uint32_t compact_capacity = 0;
    uint32_t compact_demand = 0;
    uint32_t compact_spilled = 0;
    uint32_t compact_resizes = 0;
    uint32_t compact_low_frames = 0;
    uint64_t compact_failed_tris = 0;

    RenderGraph::Pass compact_feedback_pass;

    void _create_clear_visible_pass();
    void _create_instance_cull_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_cluster_expand_args_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_cluster_expand_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_cluster_cull_args_pass();
    void _create_cluster_cull_pass();
    void _create_hiz_pass(RenderGraph::Pass& r_build, const char* p_build_name, const char* p_args);
    void _create_cluster_retest_args_pass();
    void _create_cluster_retest_pass();
    void _create_draw_compact_pass(RenderGraph::Pass& r_pass, bool p_late);
    void _create_visbuffer_compact_pass(RenderGraph::Pass& r_pass, bool p_late);
    uint32_t _compact_capacity_tris() const;
    uint64_t _compact_bound_tris() const;
    uint32_t _compact_spill_stride() const;
    void _compact_update(bool p_feedback, uint64_t p_demand);
    bool _compact_reserve(uint32_t p_tris);
    void _compact_retire(drivers::DeviceDriverVulkan::Buffer& r_buffer);
    void _compact_collect(bool p_all);
    void _create_compact_feedback_pass();

    /*****************/
    /**** TERRAIN ****/
    /*****************/

    // frustum + density + prev occlusion cull
    // raster visbuffer 1
    // restest occlusion
    // raster visbuffer 2

    /******************/
    /**** MATERIAL ****/
    /******************/

    RenderGraph::Pass material_resolve_pass;
    drivers::DeviceDriverVulkan::Pipeline material_resolve_pipe;
    void _create_material_resolve_pass();
    
    /**************/
    /**** BASE ****/
    /**************/
    
    struct CullStats {
        uint32_t refs;
        uint32_t phase1_visible;
        uint32_t retest;
        uint32_t phase2_visible;
        uint32_t instances_visible;
        uint32_t instances_occluded;
        uint32_t instances_recovered;
    };

    CullStats stats{};
    std::vector<drivers::DeviceDriverVulkan::Buffer> stats_readback;
    std::vector<uint8_t> stats_written;

    RenderGraph::Pass stats_pass;

    void _create_stats_pass();

    /*******************/
    /**** LIFECYCLE ****/
    /*******************/

    drivers::DeviceDriverVulkan::Pipeline _compute_pipeline(const wchar_t* p_resource, const char* p_name);

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
