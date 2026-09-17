#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct DebugViewFeature : Feature
{
    uint32_t view = 0;

    RenderGraph::Pass viewport_resolve_pass;
    drivers::DeviceDriverVulkan::Pipeline viewport_resolve_pipe;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}