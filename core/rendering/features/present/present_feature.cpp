#include <core/rendering/features/present/present_feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/io/embedded_resource.h>

namespace lumen {

Error PresentFeature::create_resources()
{   
    present_pass.name = "Blit";
    present_pass.category = PASS_CATEGORY_PRESENT;
    present_pass.setup = [](RenderGraph::Builder& b) {
        b.color_attachment("Backbuffer", VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.1f, 0.1f, 0.1f, 1.0f } });
        b.read_image("G_Albedo", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    };
    present_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* bb = cl.graph->image("Backbuffer");
        auto* out_color = cl.graph->image("G_Albedo");

        struct Push{
            uint32_t src_id;
            uint32_t sampler_id;
        } pc;
        pc.src_id = out_color->bindless_sampled;
        pc.sampler_id = cl.dd->default_sampler.bindless_sampler;

        cl.dd->command_render_set_viewport(cl.cmd, {{{0,0},bb->extent}});
        cl.dd->command_render_set_scissor(cl.cmd, {{{0,0},bb->extent}});
        cl.dd->command_bind_pipeline(cl.cmd, blit_pipeline);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw("blit", 3);
    };

    return Error::OK;
};

Error PresentFeature::create_pipelines()
{
    VkRenderPass rp = ctx->graph->acquire_render_pass(present_pass);

    EmbeddedResource::Blob blit_vert_blob = EmbeddedResource::load(L"SHADERS_PRESENT_BLIT_VERT");
    EmbeddedResource::Blob blit_frag_blob = EmbeddedResource::load(L"SHADERS_PRESENT_BLIT_FRAG");
    VkShaderModule blit_vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)blit_vert_blob.data, .glsl_size = blit_vert_blob.size, .name = "present/blit.vert" });
    VkShaderModule blit_fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)blit_frag_blob.data, .glsl_size = blit_frag_blob.size, .name = "present/blit.frag" });

    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.vertex_shader = blit_vs;
    pipeline_ci.fragment_shader = blit_fs;
    pipeline_ci.render_pass = rp;
    pipeline_ci.cull_mode = VK_CULL_MODE_NONE;
    pipeline_ci.name = "present/blit";
    blit_pipeline = ctx->dd->graphics_pipeline_create(pipeline_ci);

    ctx->dd->shader_free(blit_vs);
    ctx->dd->shader_free(blit_fs);

    return Error::OK;
}

void PresentFeature::destroy_resources()
{
    ctx->dd->pipeline_free(blit_pipeline);
}

void PresentFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    g.add(&present_pass);
};

}