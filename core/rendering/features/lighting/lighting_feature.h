#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/scene_gpu.h>

namespace lumen {

struct LightingFeature : Feature
{
    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipes[SHADING_TILE_TYPES];
    drivers::DeviceDriverVulkan::Sampler shadow_sampler;
    drivers::DeviceDriverVulkan::Sampler point_sampler;

    void _create_lighting_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
