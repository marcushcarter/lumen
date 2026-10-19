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
    RenderGraph graph;
    RenderPath* path = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;

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