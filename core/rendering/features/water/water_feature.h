#pragma once
#include <core/rendering/features/feature.h>
#include <core/rendering/water_gpu.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <vector>

namespace lumen {

struct WaterFeature : Feature
{
    using Buffer = drivers::DeviceDriverVulkan::Buffer;

    static constexpr uint32_t LOD_LEVELS = 5;
    static constexpr uint32_t READBACK_MAX = WaterGpuLink::READBACK_TILES;
    static constexpr float STEP_DT = 1.0f / 60.0f;

    struct Pending {
        uint64_t session = 0;
        uint64_t layout = 0;
        ivec2 tile = ivec2(0);
        VkDeviceSize offset = 0;
    };

    struct Readback {
        std::vector<Pending> tiles;
        uint64_t session = 0;
        uint64_t layout = 0;
        uint32_t stats_count = 0;
        VkDeviceSize stats_offset = 0;
    };

    struct Retired {
        Buffer buffer;
        uint64_t frame = 0;
    };

    RenderGraph::Pass sim_pass;
    RenderGraph::Pass backdrop_pass;
    RenderGraph::Pass draw_pass;
    RenderGraph::Pass debug_pass;
    drivers::DeviceDriverVulkan::Pipeline sim_pipe;
    drivers::DeviceDriverVulkan::Pipeline surface_pipe;
    drivers::DeviceDriverVulkan::Pipeline cull_pipe;
    drivers::DeviceDriverVulkan::Pipeline backdrop_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_pipe;
    drivers::DeviceDriverVulkan::Pipeline debug_pipe;

    uint64_t layout = 0;
    uint32_t capacity = 0;
    Buffer state;
    Buffer state16;
    Buffer stats;
    Buffer pool;
    Buffer masks;
    Buffer bounds;
    Buffer frozen_pool;
    Buffer frozen_masks;
    uint64_t frozen_layout = UINT64_MAX;
    uint32_t frozen_capacity = 0;
    Buffer args;
    Buffer list;
    uint32_t list_capacity = 0;
    std::vector<Buffer> host;
    std::vector<Buffer> staging;
    std::vector<Buffer> readback;
    std::vector<Readback> pending;
    std::vector<Retired> retired;
    uint64_t session = UINT64_MAX;
    uint64_t frame_counter = 0;
    uint32_t readback_cursor = 0;

    VkDeviceAddress _items_addr = 0;
    uint32_t _item_count = 0;
    uint32_t _draw_capacity = 0;
    float _lod_error = 0.01f;
    float _time = 0.0f;
    bool _caustics = true;
    bool _built = false;
    uint32_t _debug_mode = 0;

    Buffer _device_buffer(VkDeviceSize p_size, VkBufferUsageFlags p_usage, const char* p_name);
    void _retire(Buffer& r_buffer);
    void _free_retired(bool p_all);
    void _reset_sim();
    void _ensure_sim(VkCommandBuffer p_cmd, const WaterGpuLink& p_link);
    void _parse_readback(uint32_t p_frame, WaterGpuLink& r_link);
    void _record(RenderGraph::CommandList& cl, WaterGpuLink& r_link);
    void _create_passes();
    void _draw_setup(RenderGraph::Builder& b);
    void _draw_execute(RenderGraph::CommandList& cl, uint32_t p_debug_mode);
    void add_debug(RenderGraph& g, uint32_t p_mode);

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
