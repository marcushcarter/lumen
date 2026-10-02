#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct DebugViewFeature : Feature
{
    uint32_t view = 0;
    bool overdraw_active = false;

    RenderGraph::Pass overdraw_raster_pass;
    RenderGraph::Pass overdraw_raster_pass_2;
    RenderGraph::Pass viewport_resolve_pass;
    drivers::DeviceDriverVulkan::Pipeline overdraw_raster_pipe;
    drivers::DeviceDriverVulkan::Pipeline viewport_resolve_pipe;

    void _create_viewport(RenderGraph::Builder& b);
    void _overdraw_raster_setup(RenderGraph::Builder& b, uint32_t phase);
    void _overdraw_raster_execute(RenderGraph::CommandList& cl, uint32_t phase);
    void _create_overdraw_raster_passes();
    void _create_viewport_resolve_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}