#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <chrono>

namespace lumen {

struct AutoExposureFeature : Feature
{
    static constexpr uint32_t BIN_COUNT = 256;
    static constexpr VkDeviceSize HEADER_SIZE = 16;
    static constexpr VkDeviceSize BUFFER_SIZE = HEADER_SIZE + BIN_COUNT * sizeof(uint32_t);
    static constexpr float MAX_DT = 0.1f;

    float min_ev = -2.5f;
    float max_ev = 1.5f;
    float low_percentile = 0.5f;
    float high_percentile = 0.98f;
    float speed_brighten = 1.5f;
    float speed_darken = 6.0f;

    RenderGraph::Pass init_pass;
    RenderGraph::Pass histogram_pass;
    RenderGraph::Pass reduce_pass;
    drivers::DeviceDriverVulkan::Pipeline histogram_pipe;
    drivers::DeviceDriverVulkan::Pipeline reduce_pipe;
    drivers::DeviceDriverVulkan::Buffer exposure_buffer;

    bool _initialized = false;
    bool _reset = true;
    bool _was_enabled = false;
    float _dt = 0.0f;
    std::chrono::steady_clock::time_point _last_time{};

    void _create_init_pass();
    void _create_histogram_pass();
    void _create_reduce_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}