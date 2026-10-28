#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct GeometryPool;
struct MaterialCache;

struct ModelCache
{
    static constexpr uint32_t INVALID_MESH = UINT32_MAX;
    static constexpr uint32_t MAX_SLOTS = 64u * 1024;
    static constexpr uint32_t DEFAULT_MATERIAL = 0;

    struct Entry {
        Guid mesh;
        uint32_t mesh_index = INVALID_MESH;
        uint32_t slot_base = 0;
        uint32_t slot_count = 0;
        std::vector<Guid> materials;
    };

    drivers::DeviceDriverVulkan* dd = nullptr;
    uint64_t version = 0;
    uint64_t slot_version = 0;
    uint64_t _geometry_version = UINT64_MAX;
    uint64_t _material_layout = UINT64_MAX;
    uint64_t _material_bins = UINT64_MAX;
    bool _slots_dirty = false;

    std::vector<drivers::DeviceDriverVulkan::Buffer> slot_buffers;
    std::vector<uint64_t> _uploaded_version;
    std::vector<uint32_t> slot_table;
    std::unordered_map<Guid, Entry> by_guid;

    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();

    Error load(const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    void set(Guid p_guid, Guid p_mesh, const std::vector<Guid>& p_materials);
    void clear();

    void _write_slots(const Entry& p_entry, const MaterialCache& p_materials);
    void resolve(const GeometryPool& p_geometry, const MaterialCache& p_materials);
    void upload(uint32_t p_frame);

    VkDeviceAddress slot_table_address(uint32_t p_frame) const {
        return p_frame < slot_buffers.size() ? slot_buffers[p_frame].device_address : 0;
    }

    const Entry* get(Guid p_guid) const {
        auto it = by_guid.find(p_guid);
        return it == by_guid.end() ? nullptr : &it->second;
    }

    uint32_t mesh_index(Guid p_guid) const {
        const Entry* e = get(p_guid);
        return e ? e->mesh_index : INVALID_MESH;
    }

    void lookup(Guid p_guid, uint32_t& r_mesh_index, uint32_t& r_slot_base) const {
        const Entry* e = get(p_guid);
        r_mesh_index = e ? e->mesh_index : INVALID_MESH;
        r_slot_base = e ? e->slot_base : 0;
    }
};

}
