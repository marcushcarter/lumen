#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/terrain/terrain_source.h>
#include <core/rendering/scene_gpu.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace lumen {

struct TerrainCache
{
    static constexpr uint32_t FINE_SLOT_SAMPLES = lterrain_lod_samples(0) * lterrain_lod_samples(0);
    static constexpr uint32_t SMALL_SLOT_SAMPLES = lterrain_lod_samples(1) * lterrain_lod_samples(1);
    static constexpr uint32_t FINE_SLOT_WORDS = (FINE_SLOT_SAMPLES + 1) / 2;
    static constexpr uint32_t SMALL_SLOT_WORDS = (SMALL_SLOT_SAMPLES + 1) / 2;
    static constexpr uint32_t STREAMED_LODS = TERRAIN_COARSE_LEVEL;
    static constexpr uint32_t MAX_INFLIGHT = 8;
    static constexpr VkDeviceSize UPLOAD_BUDGET = 4u * 1024 * 1024;
    static constexpr float LOD_RANGE_SCALE = 4.0f;
    static constexpr float MORPH_START = 0.7f;
    static constexpr float PREFETCH_SCALE = 1.3f;

    struct Tile {
        uint32_t lod = TERRAIN_LOD_NONE;
        uint32_t slot = TERRAIN_LOD_NONE;
        uint32_t desired = TERRAIN_LOD_NONE;
        uint32_t loading = TERRAIN_LOD_NONE;
        float distance = 0.0f;
    };

    struct Request {
        uint32_t tile = 0;
        uint32_t lod = 0;
        std::vector<uint16_t> samples;
        std::atomic<bool> done{ false };
        bool ok = false;
    };

    struct Retired {
        uint64_t frame;
        uint32_t slot;
    };

    struct Upload {
        VkDeviceSize src;
        VkDeviceSize dst;
        VkDeviceSize size;
    };

    drivers::DeviceDriverVulkan* dd = nullptr;
    uint32_t frame_count = 1;
    std::unordered_map<uint64_t, std::filesystem::path> paths;

    Guid guid;
    Guid _requested;
    TerrainSource source;
    std::vector<Tile> tiles;
    vec3 origin = vec3(0.0f);
    float ranges[TERRAIN_LEVELS] = {};
    bool coarse_pending = true;
    std::vector<uint32_t> free_fine;
    std::vector<uint32_t> free_small;
    std::vector<Retired> retired;
    std::vector<std::shared_ptr<Request>> inflight;
    std::vector<std::shared_ptr<Request>> ready;
    std::vector<Upload> uploads;
    std::vector<TerrainPatch> patches;
    std::vector<TerrainTileGpu> tile_table;
    std::vector<uint32_t> _order;
    uint64_t signature = 0;
    uint64_t _activations = 0;
    float height_lo = 0.0f;
    float height_hi = 0.0f;
    uint32_t patch_count = 0;
    VkDeviceSize upload_used = 0;
    uint32_t upload_slot = 0;
    bool frame_active = false;
    const void* owner = nullptr;
    bool freeze = false;
    bool frozen = false;
    vec3 frozen_camera = vec3(0.0f);
    uint32_t frozen_count = 0;

    drivers::DeviceDriverVulkan::Buffer heights;
    std::vector<drivers::DeviceDriverVulkan::Buffer> staging;
    std::vector<drivers::DeviceDriverVulkan::Buffer> patch_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> tile_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> uniform_buffers;
    TerrainUniform _uniform{};
    drivers::DeviceDriverVulkan::Buffer quarter_indices;

    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();
    void clear();

    void register_path(Guid p_guid, const std::filesystem::path& p_path);
    void reload(Guid p_guid, const std::filesystem::path& p_path);

    void update(Guid p_terrain, const CameraUniform& p_camera, uint64_t p_frame_number, uint32_t p_slot);
    bool active() const { return frame_active && patch_count > 0; }
    bool loaded() const { return source.valid(); }

    void _activate(Guid p_terrain);
    void _deactivate();
    void _plan_streaming(const vec3& p_camera);
    void _receive(uint64_t p_frame_number);
    bool _alloc_slot(uint32_t p_lod, uint64_t p_frame_number, uint32_t& r_slot);
    void _release_slot(uint32_t p_slot, uint64_t p_frame_number);
    bool _stage(const void* p_data, VkDeviceSize p_bytes, VkDeviceSize p_dst);
    void _select(const CameraUniform& p_camera);
    bool _select_node(const CameraUniform& p_camera, uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, bool p_root);
    void _emit(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, uint32_t p_quarter);
    void _node_box(uint32_t p_tile, uint32_t p_level, uint32_t p_i, uint32_t p_j, vec3& r_min, vec3& r_max) const;
    VkDeviceSize _slot_word(uint32_t p_slot) const;
};

}
