#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct PostProcessFeature : Feature
{
    float exposure_compensation_ev = 0.0f;

    RenderGraph::Pass composite_pass;
    drivers::DeviceDriverVulkan::Pipeline composite_pipe;
    drivers::DeviceDriverVulkan::Sampler linear_sampler;

    void _create_composite_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
