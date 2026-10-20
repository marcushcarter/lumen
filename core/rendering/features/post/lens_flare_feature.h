#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <chrono>
#include <cstdint>

namespace lumen {

struct LensFlareFeature : Feature
{
    static constexpr VkDeviceSize BUFFER_SIZE = 48;
    static constexpr float VISIBILITY_SMOOTHING_S = 0.05f;
    static constexpr float MAX_DT = 0.1f;

    const uint32_t* lut_sampler = nullptr;

    RenderGraph::Pass init_pass;
    RenderGraph::Pass visibility_pass;
    drivers::DeviceDriverVulkan::Pipeline visibility_pipe;
    drivers::DeviceDriverVulkan::Buffer flare_buffer;

    bool _initialized = false;
    bool _was_enabled = false;
    bool _reset = true;
    float _dt = 0.0f;
    std::chrono::steady_clock::time_point _last_time{};

    void _create_init_pass();
    void _create_visibility_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}