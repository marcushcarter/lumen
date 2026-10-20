#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/world_gpu.h>
#include <core/assets/lmaterial.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct TextureCache;

struct MaterialCache
{
    static constexpr uint32_t INITIAL_CAPACITY = 1024;

    drivers::DeviceDriverVulkan* dd = nullptr;
    uint32_t frame_count = 1;
    uint64_t frame_number = 0;
    uint64_t version = 0;
    uint64_t layout_version = 0;
    uint64_t content_version = 0;
    uint64_t built_content = UINT64_MAX;
    uint64_t built_textures = UINT64_MAX;

    struct Entry {
        LMaterial material;
        uint32_t index = MATERIAL_DEFAULT;
    };

    struct Retired {
        uint64_t frame = 0;
        drivers::DeviceDriverVulkan::Buffer buffer;
    };

    uint32_t capacity = 0;
    std::vector<drivers::DeviceDriverVulkan::Buffer> slot_buffers;
    std::vector<uint64_t> uploaded_version;
    
    std::vector<GPUMaterial> materials;
    std::vector<uint32_t> free_slots;
    std::vector<Retired> retired;
    std::unordered_map<Guid, Entry, GuidHash, GuidEq> by_guid;
    
    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();
    void begin_frame(uint64_t p_frame_number);

    Error load(const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    Error set(const LMaterial& p_material);
    void clear();

    GPUMaterial _build(const LMaterial& p_material, const TextureCache& p_textures);
    Error _grow(uint32_t p_capacity);
    void resolve(const TextureCache& p_textures);
    void upload(uint32_t p_frame);

    static GPUMaterial default_material();

    uint32_t index_of(Guid p_guid) const {
        auto it = by_guid.find(p_guid);
        return it == by_guid.end() ? MATERIAL_DEFAULT : it->second.index;
    }

    VkDeviceAddress address(uint32_t p_frame) const {
        return p_frame < slot_buffers.size() ? slot_buffers[p_frame].device_address : 0;
    }
};

}