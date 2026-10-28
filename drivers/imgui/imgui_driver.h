#pragma once
#include <drivers/imgui/imgui_texture_cache.h>
#include <core/base/error.h>
#include <vulkan/vulkan.h>
#include <windows.h>

namespace lumen::drivers {

struct ImGuiDriverCreateInfo
{
    HWND hwnd = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t image_count = 2;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
    VkSampler sampler = VK_NULL_HANDLE;
    bool enable_docking = false;
};

struct ImGuiDriver
{
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    ImGuiTextureCache texture_cache;

    Error initialize(const ImGuiDriverCreateInfo& p_create_info);
    void shutdown();

    void begin_frame(uint64_t p_frame_number, uint32_t p_frame_count, uint64_t p_resize_epoch);
    void end_frame(uint64_t p_frame_number);
    void render();
    void record_commands(VkCommandBuffer p_cmd);
};

}