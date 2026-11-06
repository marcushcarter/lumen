#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/world_gpu.h>

namespace lumen {

struct LightingFeature : Feature
{
    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipes[SHADING_TILE_TYPES];
    drivers::DeviceDriverVulkan::Sampler shadow_sampler;
    uint32_t frame = 0;

    void _create_lighting_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
