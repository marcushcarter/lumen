#include <core/rendering/features/debug_view_feature.h>
#include <core/rendering/features/debug_view.h>
#include <core/io/embedded_resource.h>

namespace lumen {
   
Error DebugViewFeature::create_resources()
{
    viewport_resolve_pass.name = "ViewportResolve";
    viewport_resolve_pass.category = "Editor";
    viewport_resolve_pass.setup = [this](RenderGraph::Builder& b) {
        const DebugView& d = DEBUG_VIEWS[view];

        drivers::DeviceDriverVulkan::ImageCreateInfo vp_ci{};
        vp_ci.name = "Viewport";
        vp_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vp_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("Viewport", vp_ci);
        
        if (d.inputs & DebugViewInputs::SOURCE)
            b.read_image(d.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (d.inputs & DebugViewInputs::VISBUF) {
            b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }

        b.write_image("Viewport", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    };
    viewport_resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        const DebugView& d = DEBUG_VIEWS[view];
        auto out = cl.graph->image("Viewport");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            uint32_t vis_index; 
            uint32_t depth_index;
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t op, width, height;
        } pc{};
        pc.out_slot = out->bindless_storage;
        pc.op = (uint32_t)d.op;
        pc.width = out->extent.width;
        pc.height = out->extent.height;

        pc.camera_addr = cl.graph->buffer("Camera")->device_address;

        if (d.inputs & DebugViewInputs::SOURCE) {
            if (auto* s = cl.graph->image(d.image)) pc.src_index = s->bindless_sampled; else return;
        }
        if (d.inputs & DebugViewInputs::VISBUF) {
            if (auto* s = cl.graph->image("G_Visibility")) pc.vis_index = s->bindless_sampled; else return;
            if (auto* s = cl.graph->image("G_Depth")) pc.depth_index = s->bindless_sampled; else return;
            if (auto* r = cl.graph->buffer("ClusterRefs")) pc.cluster_refs_addr = r->device_address; else return;
            if (auto* r = cl.graph->buffer("Instances")) pc.instances_addr = r->device_address; else return;
        }

        uint32_t gx = (out->extent.width + 7) / 8;
        uint32_t gy = (out->extent.height + 7) / 8;

        cl.dd->command_bind_pipeline(cl.cmd, viewport_resolve_pipe);
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
    viewport_resolve_pipe = ctx->dd->compute_pipeline_create({cs, "editor/debug_view"});
    ctx->dd->shader_free(cs);
    
    return Ok;
}

void DebugViewFeature::destroy_resources()
{
    ctx->dd->pipeline_free(viewport_resolve_pipe);
}

void DebugViewFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    g.add(&viewport_resolve_pass);
};

}