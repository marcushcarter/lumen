#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>

namespace lumen {

struct LightingFeature : Feature
{
    enum : uint32_t { LIGHT_MODE_NONE, LIGHT_MODE_ZBIN, LIGHT_MODE_TILED, LIGHT_MODE_COUNT };

    const uint32_t* lut_sampler = nullptr;
    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipes[LIGHT_MODE_COUNT];

    void _create_lighting_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
