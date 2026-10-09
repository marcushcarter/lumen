// core/rendering/features/lighting/lighting_feature.h
#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>

namespace lumen {

struct LightingFeature : Feature
{
    RenderGraph::Pass lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline lighting_pipe;

    // glm::vec3 sun_direction = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
    // glm::vec3 sun_color = glm::vec3(1.0f, 0.95f, 0.88f);
    // float sun_intensity = 3.0f;
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