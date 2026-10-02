#pragma once
#include <core/rendering/features/feature.h>

namespace lumen {

struct ClusterCullFeature : Feature
{    
    static constexpr uint32_t HIZ_TILE = 64;
    static constexpr uint32_t HIZ_TILE_MIPS = 7;
    static constexpr uint32_t HIZ_TAIL_MAX = 64;

    struct CullPush
    {
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

    bool hiz_history = false;
    bool hiz_use_prev = false;
    bool hiz_ok = false;

    struct CullStats {
        uint32_t refs;
        uint32_t phase1_visible;
        uint32_t retest;
        uint32_t phase2_visible;
    };

    bool occlusion = true;
    CullStats stats{};
    std::vector<drivers::DeviceDriverVulkan::Buffer> stats_readback;
    std::vector<uint8_t> stats_written;

    RenderGraph::Pass stats_pass;

    RenderGraph::Pass clear_visible_pass;
    RenderGraph::Pass instance_cull_pass;
    RenderGraph::Pass cluster_refs_args_pass;
    RenderGraph::Pass cluster_refs_pass;
    RenderGraph::Pass cluster_cull_args_pass;
    RenderGraph::Pass cluster_cull_pass;
    RenderGraph::Pass raster_count_pass;
    RenderGraph::Pass raster_sum_pass;
    RenderGraph::Pass raster_emit_pass;
    RenderGraph::Pass raster_visibility_pass;
    RenderGraph::Pass hiz_build_pass;
    RenderGraph::Pass hiz_tail_pass;
    RenderGraph::Pass cluster_retest_args_pass;
    RenderGraph::Pass cluster_retest_pass;
    RenderGraph::Pass raster_count_pass_2;
    RenderGraph::Pass raster_sum_pass_2;
    RenderGraph::Pass raster_emit_pass_2;
    RenderGraph::Pass raster_visibility_pass_2;
    RenderGraph::Pass hiz_build_pass_2;
    RenderGraph::Pass hiz_tail_pass_2;
    RenderGraph::Pass material_resolve_pass;

    drivers::DeviceDriverVulkan::Pipeline instance_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_refs_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_refs_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline raster_count_pipe;
    drivers::DeviceDriverVulkan::Pipeline raster_sum_pipe;
    drivers::DeviceDriverVulkan::Pipeline raster_emit_pipe;
    drivers::DeviceDriverVulkan::Pipeline raster_visibility_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_tail_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_pipe;
    drivers::DeviceDriverVulkan::Pipeline material_resolve_pipe;

    void _create_clear_visible_pass();
    void _create_instance_cull_pass();
    void _create_cluster_refs_args_pass();
    void _create_cluster_refs_pass();
    void _create_cluster_cull_args_pass();
    void _create_cluster_cull_pass();
    void _create_raster_count_pass();
    void _create_raster_sum_pass();
    void _create_raster_emit_pass();
    void _create_raster_visibility_pass();
    void _create_hiz_passes(RenderGraph::Pass& r_build, RenderGraph::Pass& r_tail, const char* p_build_name, const char* p_tail_name);
    void _create_cluster_retest_args_pass();
    void _create_cluster_retest_pass();
    void _create_raster_count_2_pass();
    void _create_raster_sum_2_pass();
    void _create_raster_emit_2_pass();
    void _create_raster_visibility_2_pass();
    void _create_material_resolve_pass();
    void _create_stats_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}