#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>

namespace lumen {

struct LightingFeature : Feature
{
    enum : uint32_t { LIGHT_MODE_NONE, LIGHT_MODE_ZBIN, LIGHT_MODE_TILED, LIGHT_MODE_COUNT };

    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipes[LIGHT_MODE_COUNT];

    glm::vec3 sky_zenith = glm::vec3(0.18f, 0.32f, 0.62f);
    glm::vec3 sky_horizon = glm::vec3(0.62f, 0.72f, 0.85f);
    glm::vec3 ground_color = glm::vec3(0.12f, 0.10f, 0.08f);
    float ambient_intensity = 0.6f;

    void _create_lighting_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}