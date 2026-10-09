#pragma once
#include <core/rendering/features/feature.h>
#include <core/rendering/frame_data.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct LightCullFeature : Feature
{
    RenderGraph::Pass clear_pass;
    RenderGraph::Pass cull_pass;
    RenderGraph::Pass bin_pass;
    RenderGraph::Pass tiles_pass;
    drivers::DeviceDriverVulkan::Pipeline cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline bin_pipe;
    drivers::DeviceDriverVulkan::Pipeline tiles_pipe;

    uint32_t _local_count() const { return ctx->frame->light_count > MAX_DIRECTIONAL_LIGHTS ? ctx->frame->light_count - MAX_DIRECTIONAL_LIGHTS : 0u; }
    drivers::DeviceDriverVulkan::Pipeline _compute_pipeline(const wchar_t* p_resource, const char* p_name);

    void _create_clear_pass();
    void _create_cull_pass();
    void _create_bin_pass();
    void _create_tiles_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}