#include <core/rendering/features/editor/outline_feature.h>
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

bool OutlineFeature::_pixel_rect(VkExtent2D p_extent, uint32_t (&r_rect)[4]) const
{
    const float pad = (float)std::clamp(radius, 1, 8) + 1.0f;
    const float x0 = std::floor(rect_min.x * (float)p_extent.width - pad);
    const float y0 = std::floor(rect_min.y * (float)p_extent.height - pad);
    const float x1 = std::ceil(rect_max.x * (float)p_extent.width + pad);
    const float y1 = std::ceil(rect_max.y * (float)p_extent.height + pad);
    r_rect[0] = (uint32_t)std::clamp(x0, 0.0f, (float)p_extent.width);
    r_rect[1] = (uint32_t)std::clamp(y0, 0.0f, (float)p_extent.height);
    r_rect[2] = (uint32_t)std::clamp(x1, 0.0f, (float)p_extent.width);
    r_rect[3] = (uint32_t)std::clamp(y1, 0.0f, (float)p_extent.height);
    return r_rect[2] > r_rect[0] && r_rect[3] > r_rect[1];
}

void OutlineFeature::_create_mask_pass()
{
    mask_pass.name = "SelectionMask";
    mask_pass.category = PASS_CATEGORY_EDITOR;
    mask_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "SelectionId";
        ci.format = VK_FORMAT_R32_UINT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("SelectionId", ci);
        b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SelectionWords", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_image("SelectionId", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    mask_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* vis = cl.graph->image("G_Visibility");
        auto* depth = cl.graph->image("G_Depth");
        auto* refs = cl.graph->buffer("ClusterRefs");
        auto* instances = cl.graph->buffer("Instances");
        auto* mask = cl.graph->buffer("SelectionWords");
        auto* out = cl.graph->image("SelectionId");
        if (!vis || !depth || !refs || !instances || !mask || !out) return;

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress mask_addr;
            uint32_t word_count;
            uint32_t vis_index;
            uint32_t depth_index;
            uint32_t out_slot;
            uint32_t rect[4];
        } pc{};
        const VkExtent2D extent = { std::min(out->extent.width, vis->extent.width), std::min(out->extent.height, vis->extent.height) };
        if (!_pixel_rect(extent, pc.rect)) return;
        pc.cluster_refs_addr = refs->device_address;
        pc.instances_addr = instances->device_address;
        pc.mask_addr = mask->device_address;
        pc.word_count = pass_word_count;
        pc.vis_index = vis->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.out_slot = out->bindless_storage;

        cl.dd->command_bind_pipeline(cl.cmd, mask_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Selection mask", (pc.rect[2] - pc.rect[0] + 7) / 8, (pc.rect[3] - pc.rect[1] + 7) / 8);
    };
}

void OutlineFeature::_create_outline_pass()
{
    outline_pass.name = "SelectionOutline";
    outline_pass.category = PASS_CATEGORY_EDITOR;
    outline_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("SelectionId", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("Viewport", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    outline_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* ids = cl.graph->image("SelectionId");
        auto* out = cl.graph->image("Viewport");
        if (!ids || !out) return;

        struct Push {
            float color[4];
            uint32_t id_index;
            uint32_t out_slot;
            int32_t radius;
            uint32_t _pad;
            uint32_t rect[4];
        } pc{};
        const VkExtent2D extent = { std::min(out->extent.width, ids->extent.width), std::min(out->extent.height, ids->extent.height) };
        if (!_pixel_rect(extent, pc.rect)) return;
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
    _create_mask_pass();
    _create_outline_pass();
    return Error::OK;
}

Error OutlineFeature::create_pipelines()
{
    {
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_EDITOR_OUTLINE_MASK_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "editor/outline_mask.comp" });
    mask_pipe = ctx->dd->compute_pipeline_create({ cs, "editor/outline_mask" });
    ctx->dd->shader_free(cs);
    }
    {
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_EDITOR_OUTLINE_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "editor/outline.comp" });
    outline_pipe = ctx->dd->compute_pipeline_create({ cs, "editor/outline" });
    ctx->dd->shader_free(cs);
    }
    return Error::OK;
}

void OutlineFeature::destroy_resources()
{
    ctx->dd->pipeline_free(mask_pipe);
    ctx->dd->pipeline_free(outline_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : mask_buffers) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    mask_buffers.clear();
    slot_version.clear();
}

void OutlineFeature::build(RenderGraph& g)
{
    if (selected_count == 0 || mask_buffers.empty() || rect_max.x <= rect_min.x || rect_max.y <= rect_min.y) return;
    if (!g.image_resource("G_Visibility") || !g.image_resource("Viewport") || !g.buffer_resource("ClusterRefs")) return;

    const uint32_t slot = g.current_frame;
    drivers::DeviceDriverVulkan::Buffer& buf = mask_buffers[slot];
    const VkDeviceSize bytes = words.size() * sizeof(uint32_t);
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
        std::memcpy(buf.mapped, words.data(), bytes);
        ctx->dd->buffer_flush(buf);
        slot_version[slot] = version;
    }
    pass_word_count = (uint32_t)words.size();

    g.import_buffer("SelectionWords", &buf, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    g.add(&mask_pass);
    g.add(&outline_pass);
}

}