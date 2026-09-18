#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <core/assets/asset_common.h>
#include <core/io/embedded_resource.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <cmath>

namespace lumen {

using namespace glm;

/***************/
/**** SETUP ****/
/***************/

Error Renderer::_create_dynamic_buffers()
{
    using enum Error;

    instance_buffers.resize(frame_count);
    transform_buffers.resize(frame_count);
    camera_buffers.resize(frame_count);
    
    for (uint32_t i = 0; i < frame_count; i++) {
        instance_buffers[i] = dd->buffer_create({.size = (VkDeviceSize)MAX_INSTANCES * sizeof(Instance),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "instances"});
        transform_buffers[i] = dd->buffer_create({.size = (VkDeviceSize)MAX_INSTANCES * sizeof(Transform),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "transforms"});
        camera_buffers[i] = dd->buffer_create({.size = sizeof(CameraUniform),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "camera"});
    }

    return Ok;
}

void Renderer::_destroy_dynamic_buffers()
{
    for (uint32_t i = 0; i < frame_count; i++) {
        dd->buffer_free(instance_buffers[i]);
        dd->buffer_free(transform_buffers[i]);
        dd->buffer_free(camera_buffers[i]);
    }
}

Error Renderer::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    using enum Error;

    dd = &r_dd;
    frame_count = dd->frame_count;
    current_frame = 0;

    in_flight_fences.resize(frame_count);
    image_available_semaphores.resize(frame_count);
    command_pools.resize(frame_count);
    command_buffers.resize(frame_count);

    uint32_t graphics_family = dd->cd->graphics_queue_family;

    for (uint32_t i = 0; i < frame_count; i++) {
        in_flight_fences[i] = dd->fence_create(true);
        image_available_semaphores[i] = dd->semaphore_create();
        command_pools[i] = dd->command_pool_create(graphics_family);
        command_buffers[i] = dd->command_buffer_create(command_pools[i]);
    }

    Error err = textures.initialize(r_dd);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = geometry.initialize(r_dd);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = graph.initialize(r_dd, frame_count);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    graph.declare_image_format("Backbuffer", dd->swapchain.format);

    err = _create_dynamic_buffers();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    set_size(1, 1);
    pending_width = width;
    pending_height = height;

    return Ok;
}

void Renderer::shutdown()
{
    unload();
    
    graph.shutdown();

    _destroy_dynamic_buffers();
    _destroy_hiz_pyramid();

    for (uint32_t i = 0; i < frame_count; i++) {
        dd->fence_free(in_flight_fences[i]);
        dd->semaphore_free(image_available_semaphores[i]);
        dd->command_pool_free(command_pools[i]);
    }
}

/*****************/
/**** PROJECT ****/
/*****************/

Error Renderer::load(const std::filesystem::path& p_content_dir)
{
    using enum Error;
    
    Error err = geometry.allocate();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    std::error_code ec;
    if (!std::filesystem::exists(p_content_dir, ec)) return Ok;

    for (auto it = std::filesystem::recursive_directory_iterator(p_content_dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& path = it->path();
        if (path.extension() != ".bin") continue;
        LAssetHeader ah{};
        if (!read_asset_header(path, ah)) continue;
        switch (ah.type) {
            case AssetType::Texture:
                textures.load(ah.guid, path);
                break;
            case AssetType::Mesh:
                geometry.load(ah.guid, path);
                break;
            default:
                break;
        }
    }

    // frame.reset();
    // for (uint32_t i = 0; i < (uint32_t)geometry.meshes.size(); i++) {
    //     for (int j=0; j<10000; j++) {
    //         frame.instances_scratch.push_back(Instance{ i, (uint32_t)frame.transforms_scratch.size(), 0, 0 });
    //         frame.transforms_scratch.push_back(Transform{ translate(mat4(1.0f), glm::vec3(j, 0.0f, 0.0f)), translate(mat4(1.0f), glm::vec3(j, 0.0f, 0.0f)) });
    //         // frame.transforms_scratch.push_back(Transform{ mat4(1.0f), grid_transform(j) });
    //         frame.cluster_ref_capacity += geometry.meshes[i].cluster_count;
    //     }
    // }
    // frame.instance_count = (uint32_t)frame.instances_scratch.size();

    // for (uint32_t i = 0; i < frame_count; i++) {
    //     if (frame.instance_count != 0) {
    //         drivers::DeviceDriverVulkan::Buffer& ib = instance_buffers[i];
    //         dd->buffer_update(ib, frame.instances_scratch.data(), (VkDeviceSize)frame.instance_count * sizeof(Instance));
    //         dd->buffer_flush(ib, 0, (VkDeviceSize)frame.instance_count * sizeof(Instance));

    //         drivers::DeviceDriverVulkan::Buffer& tb = transform_buffers[i];
    //         dd->buffer_update(tb, frame.transforms_scratch.data(), (VkDeviceSize)frame.instance_count * sizeof(Transform));
    //         dd->buffer_flush(tb, 0, (VkDeviceSize)frame.instance_count * sizeof(Transform));
    //     }
    // }

    return Ok;
}

void Renderer::unload()
{
    textures.clear();
    geometry.free();
}

/****************/
/**** SIZING ****/
/****************/

void Renderer::_create_hiz_pyramid(uint32_t p_width, uint32_t p_height)
{
    uint32_t hw = (p_width + 1) / 2;
    uint32_t hh = (p_height + 1) / 2;
    uint32_t mips = 1, m = std::max(hw, hh);
    while (m > 1) { m >>= 1; mips++; }

    drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
    ci.format = VK_FORMAT_R16_SFLOAT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.mip_levels = mips;
    ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::Fixed;
    ci.fixed_width = hw;
    ci.fixed_height = hh;
    ci.pool = dd->image_persistent_pool;
    ci.name = "hiz_pyramid";
    hiz_pyramid = dd->image_create_dedicated(ci, { hw, hh });

    VkClearColorValue far_clear{};
    far_clear.float32[0] = 0.0f;
    dd->image_clear(hiz_pyramid, far_clear);
}

void Renderer::_destroy_hiz_pyramid()
{
    if (hiz_pyramid.image) dd->image_free(hiz_pyramid);
    hiz_pyramid = {};
}

Error Renderer::set_size(uint32_t p_width, uint32_t p_height)
{
    using enum Error;

    if (p_width == 0 || p_height == 0) return Ok;
    if (p_width == width && p_height == height) return Ok;
    width = p_width;
    height = p_height;
    resize_epoch++;
    
    dd->device_wait_idle();

    Error err = graph.set_size(p_width, p_height);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    _destroy_hiz_pyramid();
    _create_hiz_pyramid(p_width, p_height);

    return Ok;
}

void Renderer::request_size(uint32_t p_width, uint32_t p_height)
{
    pending_width = p_width;
    pending_height = p_height;
}

Error Renderer::apply_pending_size()
{
    if (pending_width == 0 || pending_height == 0) return Error::Ok;
    return set_size(pending_width, pending_height);
}

/****************/
/**** FRAME ****/
/****************/

static void extract_frustum_planes(const glm::mat4& vp, glm::vec4 out[6])
{
    auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    out[0] = r3 + r0;  // left
    out[1] = r3 - r0;  // right
    out[2] = r3 + r1;  // bottom
    out[3] = r3 - r1;  // top
    out[4] = r3 + r2;  // near
    out[5] = r3 - r2;  // far
    for (int i = 0; i < 6; i++) out[i] /= glm::length(glm::vec3(out[i])); // normalize
}

glm::mat4 grid_transform(uint32_t iterator, float spacing = 1.0f)
{
    constexpr uint32_t width = 10;
    constexpr uint32_t height = 10;

    uint32_t x = iterator % width;
    uint32_t y = (iterator / width) % height;
    uint32_t z = iterator / (width * height);

    return glm::translate(
        glm::mat4(1.0f),
        glm::vec3(x, y, z) * spacing
    );
}

void Renderer::_frame_build(const World& p_world)
{
    (void)p_world;

    frame.reset();

    // for (uint32_t i = 0; i < (uint32_t)geometry.meshes.size(); i++) {
    //     if (geometry.mesh_guids[i] == Guid{}) continue;
    //     frame.instances_scratch.push_back(Instance{ i, (uint32_t)frame.transforms_scratch.size(), 0, 0 });
    //     frame.transforms_scratch.push_back(Transform{ mat4(1.0f), mat4(1.0f) });
    //     frame.cluster_ref_capacity += geometry.meshes[i].cluster_count;
    // }
    // frame.instance_count = (uint32_t)frame.instances_scratch.size();

    const int GRID = 1;
    for (uint32_t i = 0; i < (uint32_t)geometry.meshes.size(); i++) {
        if (geometry.mesh_guids[i] == Guid{}) continue;
        const float spacing = geometry.meshes[i].bounds_sphere.w * 1.5f;
        const float half = (GRID - 1) * 0.5f * spacing;
        for (int gz = 0; gz < GRID; gz++) {
            for (int gx = 0; gx < GRID; gx++) {
                const vec3 pos = vec3(gx * spacing - half, 0.0f, gz * spacing - half);
                const mat4 model = translate(mat4(1.0f), pos);
                frame.instances_scratch.push_back(Instance{ i, (uint32_t)frame.transforms_scratch.size(), 0, 0 });
                frame.transforms_scratch.push_back(Transform{ model, model });
                frame.cluster_ref_capacity += geometry.meshes[i].cluster_count;
            }
        }
    }
    frame.instance_count = (uint32_t)frame.instances_scratch.size();
    
    const float aspect = height ? (float)width / (float)height : 1.0f;
    const mat4 prev_vp = frame.camera.curr_view_proj;
    frame.camera.curr_view_proj = active_camera.view_proj(aspect);
    frame.camera.prev_view_proj = camera_cut_pending ? frame.camera.curr_view_proj : prev_vp;
    frame.camera.position = vec4(active_camera.position, 1.0f);
    extract_frustum_planes(frame.camera.curr_view_proj, frame.camera.frustum_planes);
    frame.camera.near_z = active_camera.near_z;
    frame.camera.far_z = active_camera.far_z;
    frame.px_per_unit = 0.5f * (float)height / std::tan(active_camera.fov_y * 0.5f) * lod_bias;
    
    camera_cut_pending = false;
}

void Renderer::_frame_upload()
{
    if (frame.instance_count != 0) {
        drivers::DeviceDriverVulkan::Buffer& ib = instance_buffers[current_frame];
        dd->buffer_update(ib, frame.instances_scratch.data(), (VkDeviceSize)frame.instance_count * sizeof(Instance));
        dd->buffer_flush(ib, 0, (VkDeviceSize)frame.instance_count * sizeof(Instance));

        drivers::DeviceDriverVulkan::Buffer& tb = transform_buffers[current_frame];
        dd->buffer_update(tb, frame.transforms_scratch.data(), (VkDeviceSize)frame.instance_count * sizeof(Transform));
        dd->buffer_flush(tb, 0, (VkDeviceSize)frame.instance_count * sizeof(Transform));
    }
    
    drivers::DeviceDriverVulkan::Buffer& cb = camera_buffers[current_frame];
    dd->buffer_update(cb, &frame.camera, sizeof(CameraUniform));
    dd->buffer_flush(cb, 0, sizeof(CameraUniform));
}

Error Renderer::begin_frame(const World& p_world)
{
    using enum Error;
    
    auto& sc = dd->swapchain;

    Error err = dd->fence_wait(in_flight_fences[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = dd->fence_reset(in_flight_fences[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = dd->swapchain_acquire_next_image(image_available_semaphores[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    _frame_build(p_world);
    _frame_upload();

    graph.begin(current_frame);
    graph.import_image("Backbuffer", &sc.images[sc.image_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_image("HiZ", &hiz_pyramid, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    graph.import_buffer("Camera", &camera_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Geometry", &geometry.address_buffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Instances", &instance_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Transforms", &transform_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    return Ok;
}

void Renderer::compile()
{
    graph.compile();
}

Error Renderer::record()
{
    using enum Error;

    Error err = dd->command_pool_reset(command_pools[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = dd->command_buffer_begin(command_buffers[current_frame], VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    VkCommandBuffer cmd = command_buffers[current_frame];

    dd->command_bind_graphics_uniform_sets(cmd, { dd->bindless_heap.set });
    dd->command_bind_compute_uniform_sets(cmd, { dd->bindless_heap.set });

    graph.execute(cmd);
    
    err = dd->command_buffer_end(command_buffers[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    return Ok;
}

Error Renderer::end_frame()
{
    using enum Error;
    auto& sc = dd->swapchain;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available_semaphores[current_frame];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[current_frame];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &sc.present_semaphores[sc.image_index];

    VkQueue graphics_queue = dd->queue_families[dd->cd->graphics_queue_family][0].queue;
    VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]);
    LUMEN_ERR_FAIL_COND_V_MSG(result != VK_SUCCESS, Failed, "Failed to submit Vulkan queue");

    VkPresentInfoKHR present_info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &sc.present_semaphores[sc.image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &sc.swapchain;
    present_info.pImageIndices = &sc.image_index;

    VkQueue present_queue = dd->queue_families[dd->cd->present_queue_family][0].queue;
    result = vkQueuePresentKHR(present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        dd->swapchain.surface->needs_resize = true;
    } else {
        LUMEN_ERR_FAIL_COND_V_MSG(result != VK_SUCCESS, Failed, "Failed to present Vulkan queue");
    }

    current_frame = (current_frame + 1) % frame_count;
    frame_number++;

    return Ok;
}

RenderContext Renderer::make_context()
{
    RenderContext ctx{};
    ctx.dd = dd;
    ctx.graph = &graph;
    ctx.textures = &textures;
    ctx.geometry = &geometry;
    ctx.frame = &frame;
    return ctx;
}

}