#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_view.h>
#include <core/rendering/resources/texture_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/rendering/resources/model_cache.h>
#include <core/rendering/resources/material_cache.h>
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

    RenderView main_view;
    
    TextureCache textures;
    GeometryPool geometry;
    ModelCache models;
    MaterialCache materials;
    
    uint32_t frame_count = 1;
    uint32_t current_frame = 0;
    uint64_t frame_number = 0;

    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkFence> in_flight_fences;
    std::vector<VkFence> images_in_flight;
    uint64_t swapchain_generation = 0;
    std::vector<drivers::DeviceDriverVulkan::CommandPool> command_pools;
    std::vector<VkCommandBuffer> command_buffers;

    const ProfilingSettings* profiling = nullptr;

    Error initialize(drivers::DeviceDriverVulkan& r_dd, const ProfilingSettings& p_profiling);
    void shutdown();
    
    /*****************/
    /**** PROJECT ****/
    /*****************/

    Error load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir);
    void unload();

    /****************/
    /**** SIZING ****/
    /****************/
    
    static constexpr uint32_t RESIZE_SETTLE_FRAMES = 4;

    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    uint32_t settle_width = 0;
    uint32_t settle_height = 0;
    uint32_t settle_frames = 0;
    uint64_t resize_epoch = 0;

    void request_size(uint32_t p_width, uint32_t p_height);
    Error apply_pending_size();
    Error set_size(uint32_t p_width, uint32_t p_height);

    /***************/
    /**** VIEWS ****/
    /***************/

    std::vector<RenderView*> views;
    std::vector<RenderView*> _queued_views;

    Error view_create(RenderView& r_view, uint32_t p_width, uint32_t p_height);
    void view_destroy(RenderView& r_view);
    Error view_render(RenderView& r_view, const World& p_world);

    Error _view_create_buffers(RenderView& r_view);
    void _view_destroy_buffers(RenderView& r_view);
    Error _view_set_size(RenderView& r_view, uint32_t p_width, uint32_t p_height);
    void _view_create_hiz(RenderView& r_view);
    void _view_destroy_hiz(RenderView& r_view);
    void _view_collect_hiz(RenderView& r_view, bool p_all);
    void _view_reset(RenderView& r_view);
    void _view_begin(RenderView& r_view, const World& p_world);

    /***************/
    /**** FRAME ****/
    /***************/

    bool frame_acquired = false;

    void set_camera(const Camera& p_camera) { main_view.camera = p_camera; }
    void camera_cut() { main_view.camera_cut_pending = true; }

    void _frame_build(RenderView& r_view, const World& p_world);
    void _frame_upload(RenderView& r_view);

    Error acquire_frame();
    Error begin_frame(const World& p_world);
    Error compile();
    Error record();
    Error end_frame();
    
    RenderContext make_context() { return make_context(main_view); }
    RenderContext make_context(RenderView& r_view);
};

}
