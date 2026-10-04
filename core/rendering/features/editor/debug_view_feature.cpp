#include <core/rendering/features/editor/debug_view_feature.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>

namespace lumen {

static constexpr const char* OVERDRAW_DRAW_CMDS[2] = { "ClusterDrawCmds", "ClusterDrawCmds2" };
static constexpr const char* OVERDRAW_DRAW_COUNT[2] = { "ClusterDrawCount", "ClusterDrawCount2" };
static constexpr const char* OVERDRAW_DRAW_META[2] = { "ClusterDrawMeta", "ClusterDrawMeta2" };
static constexpr const char* OVERDRAW_SCATTER[2] = { "ClusterScatter", "ClusterScatter2" };

void DebugViewFeature::_create_viewport(RenderGraph::Builder& b)
{
    if (b.graph->image_resource("Viewport")) return;
    drivers::DeviceDriverVulkan::ImageCreateInfo vp_ci{};
    vp_ci.name = "Viewport";
    vp_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vp_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    b.create_image("Viewport", vp_ci);
}

void DebugViewFeature::_overdraw_raster_setup(RenderGraph::Builder& b, uint32_t phase)
{
    const VkAttachmentLoadOp load = phase == 0 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    _create_viewport(b);
    b.color_attachment("Viewport", load, VkClearValue{.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}});
    b.depth_attachment("G_Depth", load, VkClearValue{.depthStencil = {0.0f, 0}});
    b.read_buffer(OVERDRAW_DRAW_CMDS[phase], VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    b.read_buffer(OVERDRAW_DRAW_COUNT[phase], VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer(OVERDRAW_SCATTER[phase], VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer(OVERDRAW_DRAW_META[phase], VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void DebugViewFeature::_overdraw_raster_execute(RenderGraph::CommandList& cl, uint32_t phase)
{
    auto out = cl.graph->image("Viewport");
    auto draw_cmds = cl.graph->buffer(OVERDRAW_DRAW_CMDS[phase]);
    auto draw_count = cl.graph->buffer(OVERDRAW_DRAW_COUNT[phase]);

    struct Push {
        VkDeviceAddress camera_addr;
        VkDeviceAddress geometry_addr;
        VkDeviceAddress instances_addr;
        VkDeviceAddress transforms_addr;
        VkDeviceAddress cluster_refs_addr;
        VkDeviceAddress scatter_addr;
        VkDeviceAddress draw_meta_addr;
    } pc;
    pc.camera_addr = cl.graph->buffer("Camera")->device_address;
    pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
    pc.instances_addr = cl.graph->buffer("Instances")->device_address;
    pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
    pc.cluster_refs_addr = cl.graph->buffer("ClusterRefs")->device_address;
    pc.scatter_addr = cl.graph->buffer(OVERDRAW_SCATTER[phase])->device_address;
    pc.draw_meta_addr = cl.graph->buffer(OVERDRAW_DRAW_META[phase])->device_address;

    cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, out->extent }});
    cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, out->extent }});

    if (!ctx->geometry->index_buffer().buffer) return;
    cl.dd->command_bind_pipeline(cl.cmd, overdraw_raster_pipe);
    cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
    cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
    cl.draw_indexed_indirect_count(phase == 0 ? "Overdraw raster 1" : "Overdraw raster 2", *draw_cmds, 0, *draw_count, 0, ctx->geometry->cluster_extent, sizeof(VkDrawIndexedIndirectCommand));
}

void DebugViewFeature::_create_overdraw_raster_passes()
{
    overdraw_raster_pass.name = "OverdrawRaster1";
    overdraw_raster_pass.category = PASS_CATEGORY_EDITOR;
    overdraw_raster_pass.setup = [this](RenderGraph::Builder& b) { _overdraw_raster_setup(b, 0); };
    overdraw_raster_pass.execute = [this](RenderGraph::CommandList& cl) { _overdraw_raster_execute(cl, 0); };

    overdraw_raster_pass_2.name = "OverdrawRaster2";
    overdraw_raster_pass_2.category = PASS_CATEGORY_EDITOR;
    overdraw_raster_pass_2.setup = [this](RenderGraph::Builder& b) { _overdraw_raster_setup(b, 1); };
    overdraw_raster_pass_2.execute = [this](RenderGraph::CommandList& cl) { _overdraw_raster_execute(cl, 1); };
}

void DebugViewFeature::_create_viewport_resolve_pass()
{
    viewport_resolve_pass.name = "ViewportResolve";
    viewport_resolve_pass.category = PASS_CATEGORY_EDITOR;
    viewport_resolve_pass.setup = [this](RenderGraph::Builder& b) {
        const DebugView& d = DEBUG_VIEWS[view];

        _create_viewport(b);
        
        if (d.inputs & DebugViewInputs::SOURCE)
            b.read_image(d.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (d.inputs & (DebugViewInputs::VISBUF | DebugViewInputs::DEPTH))
            b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (d.inputs & DebugViewInputs::VISBUF) {
            b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }

        if (d.inputs & DebugViewInputs::GEOMETRY) {
            b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
            b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }

        VkAccessFlags2 vp_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        if (overdraw_active) vp_access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        b.write_image("Viewport", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, vp_access);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    };
    viewport_resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        const DebugView& d = DEBUG_VIEWS[view];
        if (d.op == DebugViewOp::OVERDRAW && !overdraw_active) return;
        auto out = cl.graph->image("Viewport");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress transforms_addr;
            uint32_t vis_index; 
            uint32_t depth_index;
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t op, width, height;
            float px_per_unit;
        } pc{};
        pc.out_slot = out->bindless_storage;
        pc.op = (uint32_t)d.op;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.px_per_unit = ctx->frame->px_per_unit;

        pc.camera_addr = cl.graph->buffer("Camera")->device_address;

        if (d.inputs & DebugViewInputs::SOURCE) {
            if (auto* s = cl.graph->image(d.image)) pc.src_index = s->bindless_sampled; else return;
        }
        if (d.inputs & (DebugViewInputs::VISBUF | DebugViewInputs::DEPTH)) {
            if (auto* s = cl.graph->image("G_Depth")) pc.depth_index = s->bindless_sampled; else return;
        }
        if (d.inputs & DebugViewInputs::VISBUF) {
            if (auto* s = cl.graph->image("G_Visibility")) pc.vis_index = s->bindless_sampled; else return;
            if (auto* r = cl.graph->buffer("ClusterRefs")) pc.cluster_refs_addr = r->device_address; else return;
            if (auto* r = cl.graph->buffer("Instances")) pc.instances_addr = r->device_address; else return;
        }
        if (d.inputs & DebugViewInputs::GEOMETRY) {
            if (auto* r = cl.graph->buffer("Geometry")) pc.geometry_addr = r->device_address; else return;
            if (auto* r = cl.graph->buffer("Transforms")) pc.transforms_addr = r->device_address; else return;
        }

        uint32_t gx = (out->extent.width + 7) / 8;
        uint32_t gy = (out->extent.height + 7) / 8;

        cl.dd->command_bind_pipeline(cl.cmd, viewport_resolve_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Viewport resolve", gx, gy);
    };
}
   
Error DebugViewFeature::create_resources()
{
    _create_overdraw_raster_passes();
    _create_viewport_resolve_pass();
    return Error::OK;
};

Error DebugViewFeature::create_pipelines()
{
    using enum Error;

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_EDITOR_DEBUG_VIEW_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "editor/debug_view.comp" });
    viewport_resolve_pipe = ctx->dd->compute_pipeline_create({cs, "editor/debug_view"});
    ctx->dd->shader_free(cs);
    }

    {
    ctx->graph->declare_image_format("G_Depth", VK_FORMAT_D32_SFLOAT);
    VkRenderPass rp = ctx->graph->acquire_render_pass(overdraw_raster_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_RASTER_VISBUFFER_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_EDITOR_OVERDRAW_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "raster/visbuffer.vert" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "editor/overdraw.frag" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.vertex_shader = vs; pipeline_ci.fragment_shader = fs; pipeline_ci.render_pass = rp;
    pipeline_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    pipeline_ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_ci.depth_test = true;
    pipeline_ci.depth_write = true;
    pipeline_ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    pipeline_ci.blend_modes = { drivers::DeviceDriverVulkan::BlendMode::ADDITIVE };
    pipeline_ci.name = "editor/overdraw_raster";
    overdraw_raster_pipe = ctx->dd->graphics_pipeline_create(pipeline_ci);
    ctx->dd->shader_free(vs); ctx->dd->shader_free(fs);
    }
    
    return OK;
}

void DebugViewFeature::destroy_resources()
{
    ctx->dd->pipeline_free(overdraw_raster_pipe);
    ctx->dd->pipeline_free(viewport_resolve_pipe);
}

void DebugViewFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    overdraw_active = DEBUG_VIEWS[view].op == DebugViewOp::OVERDRAW && g.buffer_resource("ClusterDrawCount2");
    if (overdraw_active) {
        g.add(&overdraw_raster_pass);
        g.add(&overdraw_raster_pass_2);
    }
    g.add(&viewport_resolve_pass);
};

}