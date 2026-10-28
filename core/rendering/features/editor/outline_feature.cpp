#include <core/rendering/features/editor/outline_feature.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstring>

namespace lumen {

void OutlineFeature::set_selection(const Entity* p_entities, uint32_t p_count)
{
    uint32_t max_index = 0;
    for (uint32_t i = 0; i < p_count; i++) max_index = std::max(max_index, p_entities[i].index);
    words.assign(p_count ? max_index / 32 + 1 : 0, 0u);
    for (uint32_t i = 0; i < p_count; i++) words[p_entities[i].index >> 5] |= 1u << (p_entities[i].index & 31);
    selected_count = p_count;
    version++;
}

static const char* _target(RenderGraph& g)
{
    if (g.image_resource("Viewport")) return "Viewport";
    if (g.image_resource("DisplayColor")) return "DisplayColor";
    return nullptr;
}

void OutlineFeature::_create_init_pass()
{
    init_pass.name = "SelectionInit";
    init_pass.category = PASS_CATEGORY_EDITOR;
    init_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = CLUSTERS_HEADER + (VkDeviceSize)MAX_CLUSTERS * sizeof(uint32_t);
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ci.device_local = true;
        b.create_buffer("SelectionClusters", ci);
        b.write_buffer("SelectionClusters", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    init_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* clusters = cl.graph->buffer("SelectionClusters");
        if (!clusters) return;
        const uint32_t header[4] = { std::max(1u, ctx->geometry->max_cluster_tris) * 3u, 0u, 0u, 0u };
        cl.update_buffer("Selection init", *clusters, header, sizeof(header), 0);
    };
}

void OutlineFeature::_create_gather_pass()
{
    gather_pass.name = "SelectionGather";
    gather_pass.category = PASS_CATEGORY_EDITOR;
    gather_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SelectionWords", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("SelectionClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    gather_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* refs = cl.graph->buffer("ClusterRefs");
        auto* args = cl.graph->buffer("ClusterCullArgs");
        auto* instances = cl.graph->buffer("Instances");
        auto* mask = cl.graph->buffer("SelectionWords");
        auto* clusters = cl.graph->buffer("SelectionClusters");
        if (!refs || !args || !instances || !mask || !clusters) return;

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress mask_addr;
            VkDeviceAddress clusters_addr;
            uint32_t capacity;
        } pc{};
        pc.cluster_refs_addr = refs->device_address;
        pc.instances_addr = instances->device_address;
        pc.mask_addr = mask->device_address;
        pc.clusters_addr = clusters->device_address;
        pc.capacity = MAX_CLUSTERS;

        cl.dd->command_bind_pipeline(cl.cmd, gather_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Selection gather", *args);
    };
}

void OutlineFeature::_create_raster_pass()
{
    raster_pass.name = "SelectionRaster";
    raster_pass.category = PASS_CATEGORY_EDITOR;
    raster_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo size_ci{};
        const char* target = _target(*b.graph);
        if (const RenderGraph::ImageResource* vp = target ? b.graph->image_resource(target) : nullptr; vp && vp->kind == RenderGraph::ResourceKind::TRANSIENT) size_ci = vp->image_create_info;

        drivers::DeviceDriverVulkan::ImageCreateInfo id_ci{};
        id_ci.sizing = size_ci.sizing;
        id_ci.width_scale = size_ci.width_scale;
        id_ci.height_scale = size_ci.height_scale;
        id_ci.fixed_width = size_ci.fixed_width;
        id_ci.fixed_height = size_ci.fixed_height;
        id_ci.name = "SelectionId";
        id_ci.format = VK_FORMAT_R32_UINT;
        id_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("SelectionId", id_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo depth_ci{};
        depth_ci.sizing = size_ci.sizing;
        depth_ci.width_scale = size_ci.width_scale;
        depth_ci.height_scale = size_ci.height_scale;
        depth_ci.fixed_width = size_ci.fixed_width;
        depth_ci.fixed_height = size_ci.fixed_height;
        depth_ci.name = "SelectionDepth";
        depth_ci.format = VK_FORMAT_D32_SFLOAT;
        depth_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        b.create_image("SelectionDepth", depth_ci);

        b.color_attachment("SelectionId", VK_ATTACHMENT_LOAD_OP_CLEAR, VkClearValue{ .color = { .uint32 = { 0u, 0u, 0u, 0u } } });
        b.depth_attachment("SelectionDepth", VK_ATTACHMENT_LOAD_OP_CLEAR, VkClearValue{ .depthStencil = { 0.0f, 0 } });
        b.read_buffer("SelectionClusters", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    raster_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* ids = cl.graph->image("SelectionId");
        auto* clusters = cl.graph->buffer("SelectionClusters");
        if (!ids || !clusters) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress clusters_addr;
        } pc{};
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.cluster_refs_addr = cl.graph->buffer("ClusterRefs")->device_address;
        pc.clusters_addr = clusters->device_address;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, ids->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, ids->extent }});
        cl.dd->command_bind_pipeline(cl.cmd, raster_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indirect("Selection raster", *clusters, 0, 1, sizeof(VkDrawIndirectCommand));
    };
}

void OutlineFeature::_create_outline_pass()
{
    outline_pass.name = "SelectionOutline";
    outline_pass.category = PASS_CATEGORY_EDITOR;
    outline_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("SelectionId", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image(_target(*b.graph), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    outline_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* ids = cl.graph->image("SelectionId");
        auto* out = cl.graph->image(_target(*cl.graph));
        if (!ids || !out) return;

        struct Push {
            float color[4];
            uint32_t id_index;
            uint32_t out_slot;
            int32_t radius;
            uint32_t _pad;
            uint32_t rect[4];
        } pc{};

        const VkExtent2D extent = { std::min(ids->extent.width, out->extent.width), std::min(ids->extent.height, out->extent.height) };
        const float pad = (float)std::clamp(radius, 1, 8) + 1.0f;
        const float x0 = std::floor(pass_rect_min.x * (float)extent.width - pad);
        const float y0 = std::floor(pass_rect_min.y * (float)extent.height - pad);
        const float x1 = std::ceil(pass_rect_max.x * (float)extent.width + pad);
        const float y1 = std::ceil(pass_rect_max.y * (float)extent.height + pad);
        pc.rect[0] = (uint32_t)std::clamp(x0, 0.0f, (float)extent.width);
        pc.rect[1] = (uint32_t)std::clamp(y0, 0.0f, (float)extent.height);
        pc.rect[2] = (uint32_t)std::clamp(x1, 0.0f, (float)extent.width);
        pc.rect[3] = (uint32_t)std::clamp(y1, 0.0f, (float)extent.height);
        if (!(pc.rect[2] > pc.rect[0] && pc.rect[3] > pc.rect[1])) return;
        pc.color[0] = color.r;
        pc.color[1] = color.g;
        pc.color[2] = color.b;
        pc.color[3] = color.a;
        pc.id_index = ids->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.radius = std::clamp(radius, 1, 8);

        cl.dd->command_bind_pipeline(cl.cmd, outline_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Selection outline", (pc.rect[2] - pc.rect[0] + 7) / 8, (pc.rect[3] - pc.rect[1] + 7) / 8);
    };
}

Error OutlineFeature::create_resources()
{
    const uint32_t count = ctx->graph->frame_count;
    mask_buffers.resize(count);
    slot_version.assign(count, UINT64_MAX);
    _create_init_pass();
    _create_gather_pass();
    _create_raster_pass();
    _create_outline_pass();
    return Error::OK;
}

Error OutlineFeature::create_pipelines()
{
    gather_pipe = _compute_pipeline(L"SHADERS_EDITOR_OUTLINE_GATHER_COMP", "editor/outline_gather.comp");
    outline_pipe = _compute_pipeline(L"SHADERS_EDITOR_OUTLINE_COMP", "editor/outline.comp");

    {
    VkRenderPass rp = ctx->graph->acquire_render_pass(raster_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_EDITOR_OUTLINE_RASTER_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_EDITOR_OUTLINE_RASTER_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "editor/outline_raster.vert" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "editor/outline_raster.frag" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.vertex_shader = vs;
    pipeline_ci.fragment_shader = fs;
    pipeline_ci.render_pass = rp;
    pipeline_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    pipeline_ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_ci.depth_test = true;
    pipeline_ci.depth_write = true;
    pipeline_ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    pipeline_ci.name = "editor/outline_raster";
    raster_pipe = ctx->dd->graphics_pipeline_create(pipeline_ci);
    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(fs);
    }

    return Error::OK;
}

void OutlineFeature::destroy_resources()
{
    ctx->dd->pipeline_free(gather_pipe);
    ctx->dd->pipeline_free(raster_pipe);
    ctx->dd->pipeline_free(outline_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : mask_buffers) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    mask_buffers.clear();
    slot_version.clear();
}

void OutlineFeature::build(RenderGraph& g)
{
    if (mask_buffers.empty()) return;
    if (selected_count == 0 || rect_max.x <= rect_min.x || rect_max.y <= rect_min.y) return;
    if (!_target(g) || !g.buffer_resource("ClusterRefs") || !g.buffer_resource("ClusterCullArgs")) return;

    const uint32_t slot = g.current_frame;
    drivers::DeviceDriverVulkan::Buffer& buf = mask_buffers[slot];
    const VkDeviceSize bytes = (words.size() + 1) * sizeof(uint32_t);
    if (!buf.buffer || buf.size < bytes) {
        if (buf.buffer) ctx->dd->buffer_free(buf);
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = std::max<VkDeviceSize>(MIN_MASK_BYTES, std::bit_ceil(bytes));
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.name = "SelectionWords";
        buf = ctx->dd->buffer_create(ci);
        LUMEN_ERR_FAIL_COND(!buf.buffer);
        slot_version[slot] = UINT64_MAX;
    }
    if (slot_version[slot] != version) {
        const uint32_t count = (uint32_t)words.size();
        std::memcpy(buf.mapped, &count, sizeof(count));
        std::memcpy((uint8_t*)buf.mapped + sizeof(count), words.data(), words.size() * sizeof(uint32_t));
        ctx->dd->buffer_flush(buf);
        slot_version[slot] = version;
    }
    pass_rect_min = rect_min;
    pass_rect_max = rect_max;

    g.import_buffer("SelectionWords", &buf, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    g.add(&init_pass);
    g.add(&gather_pass);
    g.add(&raster_pass);
    g.add(&outline_pass);
}

}
