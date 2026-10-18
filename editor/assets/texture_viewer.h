#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorContext;

struct TextureViewer
{
    enum class Channel : uint8_t { RGBA, RGB, R, G, B, A };

    static constexpr float MIN_ZOOM = 1.0f / 64.0f;
    static constexpr float MAX_ZOOM = 64.0f;
    static constexpr float ZOOM_STEP = 1.2f;
    static constexpr float CHECKER_SIZE = 12.0f;

    struct View { VkImageView view = VK_NULL_HANDLE; int32_t mip = -1; Channel channel = Channel::RGBA; };
    struct Retired { VkImageView view = VK_NULL_HANDLE; uint64_t frame = 0; };

    drivers::DeviceDriverVulkan* dd = nullptr;
    drivers::DeviceDriverVulkan::Sampler sampler_linear;
    drivers::DeviceDriverVulkan::Sampler sampler_nearest;

    Guid guid;
    bool is_open = false;
    Channel channel = Channel::RGBA;
    int32_t mip = -1;
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);

    bool _fit_pending = false;
    bool _focus_pending = false;
    VkImage _image = VK_NULL_HANDLE;
    std::vector<View> _views;
    std::vector<Retired> _retired;

    static uint32_t channel_count(VkFormat p_format);
    static VkComponentMapping swizzle(Channel p_channel, uint32_t p_channels);

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown();

    void open(Guid p_guid);
    void close(EditorContext& ctx);
    void draw(EditorContext& ctx);

    void _draw_contents(EditorContext& ctx, const drivers::DeviceDriverVulkan::Image& p_image);
    VkImageView _view(const drivers::DeviceDriverVulkan::Image& p_image, int32_t p_mip, Channel p_channel);
    void _release_views(EditorContext& ctx);
    void _collect(uint64_t p_frame_number, uint32_t p_frame_count);
};

}
