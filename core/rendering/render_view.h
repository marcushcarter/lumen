#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/frame_data.h>
#include <core/world/camera.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace lumen {

struct World;
struct RenderPath;

struct RenderView
{
    static constexpr float MIN_RENDER_SCALE = 0.25f;
    static constexpr float MAX_RENDER_SCALE = 2.0f;
    static constexpr uint32_t MIN_JITTER_PHASES = 8;
    static constexpr uint32_t MAX_JITTER_PHASES = 64;

    RenderGraph graph;
    RenderPath* path = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    float render_scale = 1.0f;
    bool jitter = false;
    uint32_t jitter_index = 0;

    std::vector<drivers::DeviceDriverVulkan::Buffer> instance_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> transform_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> camera_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> light_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> atmosphere_buffers;

    drivers::DeviceDriverVulkan::Image hiz_pyramid;
    std::vector<std::pair<uint64_t, drivers::DeviceDriverVulkan::Image>> hiz_retired;

    FrameData frame;
    const World* _last_world = nullptr;
    Camera camera;
    float lod_bias = 1.0f;
    bool camera_cut_pending = true;
    bool hiz_reset_pending = true;
};

}
