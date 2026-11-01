#pragma once
#include <core/base/error.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorContext;

struct TextureViewer
{
    static constexpr uint8_t CHANNEL_R = 1u << 0;
    static constexpr uint8_t CHANNEL_G = 1u << 1;
    static constexpr uint8_t CHANNEL_B = 1u << 2;
    static constexpr uint8_t CHANNEL_A = 1u << 3;
    static constexpr uint8_t CHANNEL_ALL = CHANNEL_R | CHANNEL_G | CHANNEL_B | CHANNEL_A;
    static constexpr uint8_t VIEW_NORMAL = 1u << 4;

    struct Retired { VkImageView view = VK_NULL_HANDLE; uint64_t frame = 0; };

    drivers::DeviceDriverVulkan* dd = nullptr;
    drivers::DeviceDriverVulkan::Sampler sampler_linear;
    drivers::DeviceDriverVulkan::Sampler sampler_nearest;
    std::vector<Retired> _retired;

    static uint32_t channel_count(VkFormat p_format);
    static uint8_t channel_mask(uint32_t p_channels);
    static VkComponentMapping swizzle(uint8_t p_mask);
    static void format_name(VkFormat p_format, char* r_out, size_t p_size);

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown();

    VkImageView create_view(const drivers::DeviceDriverVulkan::Image& p_image, int32_t p_mip, uint8_t p_mask);
    void retire(EditorContext& ctx, VkImageView p_view);
    void collect(uint64_t p_frame_number, uint32_t p_frame_count);
};

}
