#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct ThumbnailCaptureFeature : Feature
{
    static constexpr uint32_t OUTPUT_SIZE = 128;

    std::vector<drivers::DeviceDriverVulkan::Buffer> readback;
    std::vector<uint64_t> tickets;
    uint64_t request = 0;

    RenderGraph::Pass capture_pass;
    drivers::DeviceDriverVulkan::Pipeline capture_pipe;

    bool collect(uint32_t p_slot, uint64_t& r_ticket, std::vector<uint8_t>& r_pixels);
    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
