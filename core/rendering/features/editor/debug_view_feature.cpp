#include <core/rendering/features/editor/debug_view_feature.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <string_view>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>

namespace lumen {

static constexpr uint32_t INVALID_OP = 0xFFFFFFFFu;

void DebugViewFeature::_create_viewport(RenderGraph::Builder& b, const char* p_size_from)
{
    if (b.graph->image_resource("Viewport")) return;
    drivers::DeviceDriverVulkan::ImageCreateInfo vp_ci{};
    if (const RenderGraph::ImageResource* src = p_size_from ? b.graph->image_resource(p_size_from) : nullptr) {
        if (src->kind == RenderGraph::ResourceKind::IMPORTED && src->image) {
            vp_ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
            vp_ci.fixed_width = src->image->extent.width;
            vp_ci.fixed_height = src->image->extent.height;
        } else {
            vp_ci.sizing = src->image_create_info.sizing;
            vp_ci.width_scale = src->image_create_info.width_scale;
            vp_ci.height_scale = src->image_create_info.height_scale;
            vp_ci.fixed_width = src->image_create_info.fixed_width;
            vp_ci.fixed_height = src->image_create_info.fixed_height;
        }
    }
    vp_ci.name = "Viewport";
    vp_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vp_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    b.create_image("Viewport", vp_ci);
}

void DebugViewFeature::_overdraw_raster_setup(RenderGraph::Builder& b, uint32_t phase)
{
    const VkAttachmentLoadOp load = phase == 0 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    _create_viewport(b, nullptr);
    b.color_attachment("Viewport", load, VkClearValue{.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}});
    b.depth_attachment("G_Depth", load, VkClearValue{.depthStencil = {0.0f, 0}});
    b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    b.read_buffer("CompactIndices", VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
    b.read_buffer("CompactDrawArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    b.read_buffer(phase == 0 ? "VisibleClusters" : "VisibleClusters2", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void DebugViewFeature::_overdraw_raster_execute(RenderGraph::CommandList& cl, uint32_t phase)
{
    auto out = cl.graph->image("Viewport");
    auto* indices = cl.graph->buffer("CompactIndices");
    auto* args = cl.graph->buffer("CompactDrawArgs");
    struct Push {
        VkDeviceAddress camera_addr;
        VkDeviceAddress geometry_addr;
        VkDeviceAddress instances_addr;
        VkDeviceAddress transforms_addr;
        VkDeviceAddress cluster_refs_addr;
        VkDeviceAddress visible_addr;
        VkDeviceAddress prims_addr;
        VkDeviceAddress args_addr;
        uint32_t phase;
    } pc{};
    pc.camera_addr = cl.graph->buffer("Camera")->device_address;
    pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
    pc.instances_addr = cl.graph->buffer("Instances")->device_address;
    pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
    pc.cluster_refs_addr = cl.graph->buffer("ClusterRefs")->device_address;
    pc.visible_addr = cl.graph->buffer(phase == 0 ? "VisibleClusters" : "VisibleClusters2")->device_address;
    pc.args_addr = args->device_address;
    pc.phase = phase;

    cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, out->extent }});
    cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, out->extent }});
    cl.dd->command_bind_pipeline(cl.cmd, overdraw_compact_pipe);
    cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
    cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
    cl.draw_indexed_indirect(phase == 0 ? "Overdraw raster 1" : "Overdraw raster 2", *args, phase * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
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
        const DebugView& d = DEBUG_VIEWS[pass_view];

        _create_viewport(b, d.op == DebugViewOp::COPY ? d.image : nullptr);
        
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
        
        if ((d.inputs & DebugViewInputs::LIGHTS) && b.graph->buffer_resource("LightCullData")) {
            b.read_buffer("LightCullData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            if (b.graph->buffer_resource("LightTileMasks")) b.read_buffer("LightTileMasks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.read_buffer("SortedLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }

        VkAccessFlags2 vp_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        if (overdraw_active) vp_access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        b.write_image("Viewport", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, vp_access);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    };
    viewport_resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        const DebugView& d = DEBUG_VIEWS[pass_view];
        auto out = cl.graph->image("Viewport");
        if (!out) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress light_cull_addr;
            VkDeviceAddress light_masks_addr;
            VkDeviceAddress light_sorted_addr;
            uint32_t vis_index; 
            uint32_t depth_index;
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t op, width, height;
            float px_per_unit;
            uint32_t has_local_lights;
        } pc{};
        pc.out_slot = out->bindless_storage;
        pc.op = (uint32_t)d.op;
        if (d.op == DebugViewOp::OVERDRAW && !overdraw_active) pc.op = INVALID_OP;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.px_per_unit = ctx->frame->px_per_unit;

        pc.camera_addr = cl.graph->buffer("Camera")->device_address;

        if (d.inputs & DebugViewInputs::SOURCE) {
            if (auto* s = cl.graph->image(d.image)) pc.src_index = s->bindless_sampled; else pc.op = INVALID_OP;
        }
        if (d.inputs & (DebugViewInputs::VISBUF | DebugViewInputs::DEPTH)) {
            if (auto* s = cl.graph->image("G_Depth")) pc.depth_index = s->bindless_sampled; else pc.op = INVALID_OP;
        }
        if (d.inputs & DebugViewInputs::VISBUF) {
            if (auto* s = cl.graph->image("G_Visibility")) pc.vis_index = s->bindless_sampled; else pc.op = INVALID_OP;
            if (auto* r = cl.graph->buffer("ClusterRefs")) pc.cluster_refs_addr = r->device_address; else pc.op = INVALID_OP;
            if (auto* r = cl.graph->buffer("Instances")) pc.instances_addr = r->device_address; else pc.op = INVALID_OP;
        }
        if (d.inputs & DebugViewInputs::GEOMETRY) {
            if (auto* r = cl.graph->buffer("Geometry")) pc.geometry_addr = r->device_address; else pc.op = INVALID_OP;
            if (auto* r = cl.graph->buffer("Transforms")) pc.transforms_addr = r->device_address; else pc.op = INVALID_OP;
        }
        if (d.inputs & DebugViewInputs::LIGHTS) {
            auto* cull = cl.graph->buffer("LightCullData");
            auto* masks = cl.graph->buffer("LightTileMasks");
            auto* sorted = cl.graph->buffer("SortedLights");
            pc.has_local_lights = cull && sorted ? 1u : 0u;
            if (pc.has_local_lights) {
                pc.light_cull_addr = cull->device_address;
                pc.light_masks_addr = masks ? masks->device_address : 0;
                pc.light_sorted_addr = sorted->device_address;
            }
        }

        uint32_t gx = (out->extent.width + 7) / 8;
        uint32_t gy = (out->extent.height + 7) / 8;

        if (pc.op == (uint32_t)DebugViewOp::COPY) {
            struct {
                uint32_t src_index;
                uint32_t out_slot;
                uint32_t width;
                uint32_t height;
            } copy_pc{ pc.src_index, pc.out_slot, pc.width, pc.height };
            cl.dd->command_bind_pipeline(cl.cmd, viewport_copy_pipe);
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(copy_pc), &copy_pc);
            cl.dispatch("Viewport copy", gx, gy);
            return;
        }

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
    viewport_resolve_pipe = _compute_pipeline(L"SHADERS_EDITOR_DEBUG_VIEW_COMP", "editor/debug_view.comp");
    viewport_copy_pipe = _compute_pipeline(L"SHADERS_EDITOR_VIEWPORT_COPY_COMP", "editor/viewport_copy.comp");

    {
    ctx->graph->declare_image_format("G_Depth", VK_FORMAT_D32_SFLOAT);
    VkRenderPass rp = ctx->graph->acquire_render_pass(overdraw_raster_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_RASTER_VISBUFFER_COMPACT_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_EDITOR_OVERDRAW_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "raster/visbuffer_compact.vert" });
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
    overdraw_compact_pipe = ctx->dd->graphics_pipeline_create(pipeline_ci);
    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(fs);
    }
    
    return Error::OK;
}

void DebugViewFeature::destroy_resources()
{
    ctx->dd->pipeline_free(overdraw_compact_pipe);
    ctx->dd->pipeline_free(viewport_resolve_pipe);
    ctx->dd->pipeline_free(viewport_copy_pipe);
}

void DebugViewFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    pass_view = view < (uint32_t)DEBUG_VIEW_COUNT ? view : 0;
    overdraw_active = DEBUG_VIEWS[pass_view].op == DebugViewOp::OVERDRAW && g.buffer_resource("CompactDrawArgs");
    if (overdraw_active) {
        g.add(&overdraw_raster_pass);
        g.add(&overdraw_raster_pass_2);
    }
    const DebugView& d = DEBUG_VIEWS[pass_view];
    if (d.op == DebugViewOp::COPY && d.image && std::string_view(d.image) == "DisplayColor") return;
    g.add(&viewport_resolve_pass);
};

}