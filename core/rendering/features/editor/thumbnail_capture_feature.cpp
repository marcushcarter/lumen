#include <core/rendering/features/editor/thumbnail_capture_feature.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <core/io/embedded_resource.h>
#include <cstring>

namespace lumen {

Error ThumbnailCaptureFeature::create_resources()
{
    using enum Error;
    const uint32_t slots = ctx->graph->frame_count;
    readback.resize(slots);
    tickets.assign(slots, 0);
    for (uint32_t i = 0; i < slots; ++i) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.name = "Thumbnail_Readback";
        ci.size = (VkDeviceSize)OUTPUT_SIZE * OUTPUT_SIZE * 4;
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.cpu_read = true;
        readback[i] = ctx->dd->buffer_create(ci);
        if (!readback[i].buffer || !readback[i].mapped) return FAILED;
    }

    capture_pass.name = "ThumbnailCapture";
    capture_pass.category = PASS_CATEGORY_EDITOR;
    capture_pass.never_cull = true;
    capture_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image(PostProcessFeature::DISPLAY_COLOR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_buffer("ThumbnailReadback", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    capture_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* src = cl.graph->image(PostProcessFeature::DISPLAY_COLOR);
        auto* dst = cl.graph->buffer("ThumbnailReadback");
        if (!src || !dst) return;

        struct Push {
            uint64_t out_addr;
            uint32_t src_index;
            uint32_t src_width;
            uint32_t src_height;
            uint32_t out_size;
        } pc{};
        pc.out_addr = dst->device_address;
        pc.src_index = src->bindless_sampled;
        pc.src_width = src->extent.width;
        pc.src_height = src->extent.height;
        pc.out_size = OUTPUT_SIZE;

        cl.dd->command_bind_pipeline(cl.cmd, capture_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("ThumbnailCapture", (OUTPUT_SIZE + 7) / 8, (OUTPUT_SIZE + 7) / 8);
    };
    return OK;
}

Error ThumbnailCaptureFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_EDITOR_THUMBNAIL_CAPTURE_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "editor/thumbnail_capture.comp" });
    capture_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "editor/thumbnail_capture" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void ThumbnailCaptureFeature::destroy_resources()
{
    ctx->dd->pipeline_free(capture_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : readback) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    readback.clear();
    tickets.clear();
}

bool ThumbnailCaptureFeature::collect(uint32_t p_slot, uint64_t& r_ticket, std::vector<uint8_t>& r_pixels)
{
    if (p_slot >= tickets.size() || tickets[p_slot] == 0) return false;
    drivers::DeviceDriverVulkan::Buffer& rb = readback[p_slot];
    const size_t bytes = (size_t)OUTPUT_SIZE * OUTPUT_SIZE * 4;
    ctx->dd->buffer_invalidate(rb, 0, bytes);
    r_pixels.resize(bytes);
    std::memcpy(r_pixels.data(), rb.mapped, bytes);
    r_ticket = tickets[p_slot];
    tickets[p_slot] = 0;
    return true;
}

void ThumbnailCaptureFeature::build(RenderGraph& g)
{
    const uint32_t slot = g.current_frame;
    if (request == 0 || slot >= readback.size() || !g.image_resource(PostProcessFeature::DISPLAY_COLOR)) return;
    g.import_buffer("ThumbnailReadback", &readback[slot], VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    g.add(&capture_pass);
    tickets[slot] = request;
    request = 0;
}

}