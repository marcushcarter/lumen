#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct DebugLinesFeature : Feature
{
    static constexpr float LINE_WIDTH_PX = 1.5f;
    static constexpr VkDeviceSize INITIAL_LINES = 4096;

    uint32_t mask = 0;
    float width = LINE_WIDTH_PX;

    RenderGraph::Pass lines_pass;
    drivers::DeviceDriverVulkan::Pipeline lines_pipe;
    std::vector<drivers::DeviceDriverVulkan::Buffer> line_buffers;

    uint32_t _upload(drivers::DeviceDriverVulkan::Buffer& r_buffer);
    void _create_lines_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
