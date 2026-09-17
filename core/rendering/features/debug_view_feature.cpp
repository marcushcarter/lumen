#include <core/rendering/features/debug_view_feature.h>
#include <core/rendering/features/debug_view.h>
#include <core/io/embedded_resource.h>

namespace lumen {
   
Error DebugViewFeature::create_resources()
{
    pass.name = "ViewportResolve";
    pass.category = "Editor";
    pass.setup = [this](RenderGraph::Builder& b) {
        const DebugView& d = DEBUG_VIEWS[view];

        drivers::DeviceDriverVulkan::ImageCreateInfo vp_ci{};
        vp_ci.name = "Viewport";
        vp_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vp_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("Viewport", vp_ci);
        
        if (d.inputs & DebugViewInputs::SOURCE)
            b.read_image(d.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        b.write_image("Viewport", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    pass.execute = [this](RenderGraph::CommandList& cl) {
        const DebugView& d = DEBUG_VIEWS[view];
        auto out = cl.graph->image("Viewport");

        struct Push {
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t op;
        } pc{};

        if (d.inputs & DebugViewInputs::SOURCE) pc.src_index = cl.graph->image(d.image)->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.op = (uint32_t)d.op;

        uint32_t gx = (out->extent.width + 7) / 8;
        uint32_t gy = (out->extent.height + 7) / 8;

        cl.dd->command_bind_pipeline(cl.cmd, pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Viewport resolve", gx, gy);
    };

    return Error::Ok;
};

Error DebugViewFeature::create_pipelines()
{
    using enum Error;

    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_EDITOR_DEBUG_VIEW_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "editor/debug_view.comp" });
    pipe = ctx->dd->compute_pipeline_create({cs, "editor/debug_view"});
    ctx->dd->shader_free(cs);
    
    return Ok;
}

void DebugViewFeature::destroy_resources()
{
    ctx->dd->pipeline_free(pipe);
}

void DebugViewFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    g.add(&pass);
};

}