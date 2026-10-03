#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <future>
#include <filesystem>

namespace lumen {

struct ScreenshotFeature : Feature
{    
    RenderGraph::Pass screenshot_pass;

    bool requested = false;
    bool pending = false;

    drivers::DeviceDriverVulkan::Buffer staging;
    uint32_t capture_width = 0;
    uint32_t capture_height = 0;
    VkFormat capture_format = VK_FORMAT_UNDEFINED;

    std::future<bool> encode_job;
    std::filesystem::path encode_path;

    void _writeback();

    Error create_resources() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}