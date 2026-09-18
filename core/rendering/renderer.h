#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/resources/texture_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/render_context.h>
#include <core/rendering/world_gpu.h>
#include <core/world/camera.h>
#include <core/base/error.h>

namespace lumen {

struct World;

struct Renderer
{
    drivers::DeviceDriverVulkan* dd = nullptr;

    /***************/
    /**** SETUP ****/
    /***************/

    RenderGraph graph;
    
    TextureCache textures;
    GeometryPool geometry;
    
    uint32_t frame_count = 1;
    uint32_t current_frame = 0;
    uint64_t frame_number = 0;

    float lod_bias = 0.6f;

    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkFence> in_flight_fences;
    std::vector<drivers::DeviceDriverVulkan::CommandPool> command_pools;
    std::vector<VkCommandBuffer> command_buffers;

    Error _create_dynamic_buffers();
    void _destroy_dynamic_buffers();

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown();
    
    /*****************/
    /**** PROJECT ****/
    /*****************/

    Error load(const std::filesystem::path& p_content_dir);
    void unload();

    /****************/
    /**** SIZING ****/
    /****************/
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    uint64_t resize_epoch = 0;

    drivers::DeviceDriverVulkan::Image hiz_pyramid;

    void _create_hiz_pyramid(uint32_t p_width, uint32_t p_height);
    void _destroy_hiz_pyramid();

    void request_size(uint32_t p_width, uint32_t p_height);
    Error apply_pending_size();
    Error set_size(uint32_t p_width, uint32_t p_height);

    /****************/
    /**** FRAME ****/
    /****************/

    FrameData frame;

    Camera active_camera;
    bool camera_cut_pending = true;

    void set_camera(const Camera& p_camera) { active_camera = p_camera; }
    void camera_cut() { camera_cut_pending = true; }

    std::vector<drivers::DeviceDriverVulkan::Buffer> instance_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> transform_buffers;
    std::vector<drivers::DeviceDriverVulkan::Buffer> camera_buffers;

    void _frame_build(const World& p_world);
    void _frame_upload();

    Error begin_frame(const World& p_world);
    void compile();
    Error record();
    Error end_frame();
    
    RenderContext make_context();
};

}