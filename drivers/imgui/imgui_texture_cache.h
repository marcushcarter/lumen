#pragma once
#include <core/base/error.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace lumen::drivers {

struct ImGuiTextureCache
{
    struct Key
    {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash
    {
        size_t operator()(const Key& p_key) const noexcept { return std::hash<uint64_t>{}((uint64_t)p_key.view) ^ (std::hash<uint64_t>{}((uint64_t)p_key.sampler) * 0x9E3779B97F4A7C15ull); }
    };
    struct Entry { VkDescriptorSet set = VK_NULL_HANDLE; uint64_t last_used_frame = 0; };
    struct Retired { VkDescriptorSet set = VK_NULL_HANDLE; uint64_t retire_frame = 0; };

    uint64_t frame_number = 0;
    uint32_t frame_count = 1;
    uint64_t seen_resize_epoch = 0;

    std::unordered_map<Key, Entry, KeyHash> entries;
    std::vector<Retired> retired;

    VkSampler sampler = VK_NULL_HANDLE;

    Error initialize(VkSampler p_sampler);
    void _invalidate_all();
    void shutdown();

    void begin_frame(uint64_t p_frame_number, uint32_t p_frame_count, uint64_t p_resize_epoch);

    VkDescriptorSet get(VkImageView p_view, VkSampler p_sampler = VK_NULL_HANDLE);
    void retire(VkImageView p_view, uint64_t p_frame_number, uint32_t p_frame_count);
    void retire_all();
    void collect(uint64_t frame_number);
};

}