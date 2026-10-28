#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/world_gpu.h>
#include <glm/glm.hpp>

namespace lumen {

struct LightingFeature : Feature
{
    enum class LightMode : uint32_t { NONE, ZBIN, TILED, COUNT };

    static constexpr uint32_t LIGHT_MODE_COUNT = (uint32_t)LightMode::COUNT;

    const uint32_t* lut_sampler = nullptr;
    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipes[SHADING_TILE_TYPES][LIGHT_MODE_COUNT];

    void _create_lighting_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
