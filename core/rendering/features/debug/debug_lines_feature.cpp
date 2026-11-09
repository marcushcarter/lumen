#include <core/rendering/features/debug/debug_lines_feature.h>
#include <core/rendering/features/editor/editor_overlay.h>
#include <core/rendering/debug_draw.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cstring>

namespace lumen {

static constexpr const char* LINES_FALLBACK_TARGET = "Viewport";

uint32_t DebugLinesFeature::_upload(drivers::DeviceDriverVulkan::Buffer& r_buffer)
{
    const std::vector<DebugLine>& src = ctx->frame->debug_lines;
    if (src.empty()) return 0;

    const uint32_t count = std::min((uint32_t)src.size(), DebugDraw::MAX_LINES);
    if (ctx->dd->buffer_ensure_capacity(r_buffer, (VkDeviceSize)count * sizeof(DebugLine)) != Error::OK || !r_buffer.mapped) return 0;

    DebugLine* dst = (DebugLine*)r_buffer.mapped;
    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        if ((src[i].flags & mask & DebugDraw::CATEGORY_MASK) == 0) continue;
        dst[written++] = src[i];
    }
    if (written) ctx->dd->buffer_flush(r_buffer, 0, (VkDeviceSize)written * sizeof(DebugLine));
    return written;
}

void DebugLinesFeature::_create_lines_pass()
{
    lines_pass.name = "DebugLines";
    lines_pass.category = PASS_CATEGORY_EDITOR;
    lines_pass.setup = [](RenderGraph::Builder& b) {
        const char* target = editor_overlay_target(*b.graph);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.color_attachment(target ? target : LINES_FALLBACK_TARGET, VK_ATTACHMENT_LOAD_OP_LOAD);
    };
    lines_pass.execute = [this](RenderGraph::CommandList& cl) {
        const char* target = editor_overlay_target(*cl.graph);
        auto* out = target ? cl.graph->image(target) : nullptr;
        auto* depth = cl.graph->image("G_Depth");
        auto* camera = cl.graph->buffer("Camera");
        if (!out || !depth || !camera) return;

        drivers::DeviceDriverVulkan::Buffer& buf = line_buffers[cl.graph->current_frame];
        const uint32_t count = _upload(buf);
        if (count == 0) return;

        struct Push {
            VkDeviceAddress lines_addr;
            VkDeviceAddress camera_addr;
            uint32_t out_width;
            uint32_t out_height;
            uint32_t depth_width;
            uint32_t depth_height;
            uint32_t depth_index;
            float half_width;
        } pc{};
        pc.lines_addr = buf.device_address;
        pc.camera_addr = camera->device_address;
        pc.out_width = out->extent.width;
        pc.out_height = out->extent.height;
        pc.depth_width = depth->extent.width;
        pc.depth_height = depth->extent.height;
        pc.depth_index = depth->bindless_sampled;
        pc.half_width = width * 0.5f;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, out->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, out->extent }});
        cl.dd->command_bind_pipeline(cl.cmd, lines_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw("Debug lines", count * 6);
    };
}

Error DebugLinesFeature::create_resources()
{
    line_buffers.resize(ctx->graph->frame_count);
    for (drivers::DeviceDriverVulkan::Buffer& b : line_buffers) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = INITIAL_LINES * sizeof(DebugLine);
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.name = "DebugLines";
        b = ctx->dd->buffer_create(ci);
        LUMEN_ERR_FAIL_COND_V(!b.buffer, Error::FAILED);
    }
    _create_lines_pass();
    return Error::OK;
}

Error DebugLinesFeature::create_pipelines()
{
    ctx->graph->declare_image_format(LINES_FALLBACK_TARGET, VK_FORMAT_R16G16B16A16_SFLOAT);
    VkRenderPass rp = ctx->graph->acquire_render_pass(lines_pass);

    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_DEBUG_LINES_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_DEBUG_LINES_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "debug/lines.vert" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "debug/lines.frag" });

    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.render_pass = rp;
    ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.cull_mode = VK_CULL_MODE_NONE;
    ci.depth_test = false;
    ci.depth_write = false;
    ci.blend_modes = { drivers::DeviceDriverVulkan::BlendMode::ALPHA };
    ci.name = "debug/lines";
    lines_pipe = ctx->dd->graphics_pipeline_create(ci);

    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(fs);
    return Error::OK;
}

void DebugLinesFeature::destroy_resources()
{
    ctx->dd->pipeline_free(lines_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : line_buffers) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    line_buffers.clear();
}

void DebugLinesFeature::build(RenderGraph& g)
{
    if (mask == 0 || line_buffers.empty()) return;
    if (!editor_overlay_target(g) || !g.image_resource("G_Depth")) return;
    g.add(&lines_pass);
}

}
