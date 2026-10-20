#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <filesystem>

namespace lumen {

struct LTexture {
    Guid guid;
    drivers::DeviceDriverVulkan::Image image;
};

struct TextureCache
{
    drivers::DeviceDriverVulkan* dd = nullptr;
    uint32_t frame_count = 1;
    uint64_t frame_number = 0;
    uint64_t version = 0;

    struct Retired {
        uint64_t frame;
        drivers::DeviceDriverVulkan::Image image;
    };

    std::vector<LTexture> slots;
    std::vector<uint32_t> free_slots;
    std::vector<Retired> retired;
    std::unordered_map<Guid, uint32_t, GuidHash, GuidEq> by_guid;

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown();
    void begin_frame(uint64_t p_frame_number, uint32_t p_frame_count);
    
    uint32_t load(Guid p_guid, const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    void clear();

    const LTexture* get(Guid p_guid) const {
        auto it = by_guid.find(p_guid);
        return it == by_guid.end() ? nullptr : &slots[it->second];
    }

    uint32_t bindless_of(Guid p_guid) const {
        const LTexture* t = get(p_guid);
        return t ? t->image.bindless_sampled : UINT32_MAX;
    }
};
    
}