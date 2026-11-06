#pragma once
#include <core/rendering/features/feature.h>
#include <core/rendering/world_gpu.h>

namespace lumen {

struct ShadowFeature : Feature
{
    static constexpr const char* CASCADE_NAMES[SHADOW_CASCADES] = { "SunShadow0", "SunShadow1" };
    static constexpr uint32_t LIST_HEADER = 4;
    static constexpr uint32_t CHUNK = 64;
    static constexpr uint32_t CLUSTER_CAPACITY = 512u * 1024;
    static constexpr uint32_t TRI_CAPACITY = 2u * 1024 * 1024;
    static constexpr uint32_t ARGS_STRIDE = 12;
    static constexpr uint32_t ARGS_SIZE = ARGS_STRIDE * sizeof(uint32_t);
    static constexpr uint32_t ARGS_BUILD_OFFSET = 6 * sizeof(uint32_t);
    static constexpr uint32_t DRAW_CMD_SIZE = 20;
    static constexpr float LOD_TEXELS = 1.5f;
    static constexpr float LOD_MIN_ERROR = SHADOW_SOFTNESS * 0.5f;
    static constexpr float DEPTH_BIAS_CONSTANT = 2.0f;
    static constexpr float DEPTH_BIAS_SLOPE = 2.5f;

    RenderGraph::Pass clear_pass;
    RenderGraph::Pass instance_cull_pass;
    RenderGraph::Pass list_args_pass;
    RenderGraph::Pass cluster_cull_pass;
    RenderGraph::Pass draw_args_pass;
    RenderGraph::Pass index_build_pass;
    RenderGraph::Pass cascade_passes[SHADOW_CASCADES];

    drivers::DeviceDriverVulkan::Pipeline instance_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cluster_cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline index_build_pipe;
    drivers::DeviceDriverVulkan::Pipeline depth_pipe;

    uint32_t cascade_mask = 0;

    uint32_t _list_capacity() const;
    uint32_t _list_stride() const;
    void _create_clear_pass();
    void _create_instance_cull_pass();
    void _create_args_pass(RenderGraph::Pass& r_pass, uint32_t p_phase);
    void _create_cluster_cull_pass();
    void _create_index_build_pass();
    void _create_cascade_pass(uint32_t p_index);

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
