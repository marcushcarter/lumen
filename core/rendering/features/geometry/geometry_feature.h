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

    bool occlusion = true;
    bool contribution_culling = true;
    float contribution_px = 1.0f;
    bool hiz_history = false;
    bool hiz_use_prev = false;
    bool hiz_ok = false;

    RenderGraph::Pass clear_visible_pass;
    RenderGraph::Pass instance_cull_pass;
    RenderGraph::Pass cluster_expand_args_pass;
    RenderGraph::Pass cluster_expand_pass;
    RenderGraph::Pass cluster_cull_args_pass;
    RenderGraph::Pass cluster_cull_pass;
    RenderGraph::Pass draw_count_pass;
    RenderGraph::Pass draw_build_pass;
    RenderGraph::Pass draw_scatter_pass;
    RenderGraph::Pass visbuffer_pass;
    RenderGraph::Pass hiz_build_pass;
    RenderGraph::Pass hiz_tail_pass;
    RenderGraph::Pass cluster_retest_args_pass;
    RenderGraph::Pass cluster_retest_pass;
    RenderGraph::Pass draw_count_pass_2;
    RenderGraph::Pass draw_build_pass_2;
    RenderGraph::Pass draw_scatter_pass_2;
    RenderGraph::Pass visbuffer_pass_2;
    RenderGraph::Pass hiz_build_pass_2;
    RenderGraph::Pass hiz_tail_pass_2;

    drivers::DeviceDriverVulkan::Pipeline instance_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_expand_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_expand_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_count_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_scatter_pipe;
    drivers::DeviceDriverVulkan::Pipeline visbuffer_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline hiz_tail_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_retest_pipe;

    void _create_clear_visible_pass();
    void _create_instance_cull_pass();
    void _create_cluster_expand_args_pass();
    void _create_cluster_expand_pass();
    void _create_cluster_cull_args_pass();
    void _create_cluster_cull_pass();
    void _create_draw_count_pass();
    void _create_draw_build_pass();
    void _create_draw_scatter_pass();
    void _create_visbuffer_pass();
    void _create_hiz_passes(RenderGraph::Pass& r_build, RenderGraph::Pass& r_tail, const char* p_build_name, const char* p_tail_name);
    void _create_cluster_retest_args_pass();
    void _create_cluster_retest_pass();
    void _create_draw_count_2_pass();
    void _create_draw_build_2_pass();
    void _create_draw_scatter_2_pass();
    void _create_visbuffer_2_pass();

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
    };

    CullStats stats{};
    std::vector<drivers::DeviceDriverVulkan::Buffer> stats_readback;
    std::vector<uint8_t> stats_written;

    RenderGraph::Pass stats_pass;

    void _create_stats_pass();

    /*******************/
    /**** LIFECYCLE ****/
    /*******************/

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
