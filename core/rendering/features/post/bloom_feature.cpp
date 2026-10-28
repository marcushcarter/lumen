#include <core/rendering/features/post/bloom_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <string>

namespace lumen {

const char* BloomFeature::_up_source(uint32_t p_mip) const
{
    return p_mip + 2 == mip_count ? DOWN_NAMES[p_mip + 1] : UP_NAMES[p_mip + 1];
}

void BloomFeature::_create_down_pass(uint32_t p_mip)
{
    RenderGraph::Pass& pass = down_passes[p_mip];
    pass.name = "BloomDown" + std::to_string(p_mip);
    pass.category = PASS_CATEGORY_POST;
    pass.setup = [this, p_mip](RenderGraph::Builder& b) {
        const char* src = p_mip == 0 ? source : DOWN_NAMES[p_mip - 1];
        b.create_image(DOWN_NAMES[p_mip], _storage_image_info(DOWN_NAMES[p_mip], mip_width[p_mip], mip_height[p_mip]));
        b.read_image(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image(DOWN_NAMES[p_mip], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    pass.execute = [this, p_mip](RenderGraph::CommandList& cl) {
        auto* src = cl.graph->image(p_mip == 0 ? source : DOWN_NAMES[p_mip - 1]);
        auto* out = cl.graph->image(DOWN_NAMES[p_mip]);
        if (!src || !out) return;

        struct Push {
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t sampler_index;
            uint32_t karis;
            uint32_t out_width;
            uint32_t out_height;
            uint32_t src_width;
            uint32_t src_height;
        } pc{};
        pc.src_index = src->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.sampler_index = linear_sampler.bindless_sampler;
        pc.karis = p_mip == 0 ? 1u : 0u;
        pc.out_width = out->extent.width;
        pc.out_height = out->extent.height;
        pc.src_width = src->extent.width;
        pc.src_height = src->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, down_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Bloom down", (pc.out_width + 7) / 8, (pc.out_height + 7) / 8);
    };
}

void BloomFeature::_create_up_pass(uint32_t p_mip)
{
    RenderGraph::Pass& pass = up_passes[p_mip];
    pass.name = "BloomUp" + std::to_string(p_mip);
    pass.category = PASS_CATEGORY_POST;
    pass.setup = [this, p_mip](RenderGraph::Builder& b) {
        b.create_image(UP_NAMES[p_mip], _storage_image_info(UP_NAMES[p_mip], mip_width[p_mip], mip_height[p_mip]));
        b.read_image(DOWN_NAMES[p_mip], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image(_up_source(p_mip), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image(UP_NAMES[p_mip], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    pass.execute = [this, p_mip](RenderGraph::CommandList& cl) {
        auto* base = cl.graph->image(DOWN_NAMES[p_mip]);
        auto* low = cl.graph->image(_up_source(p_mip));
        auto* out = cl.graph->image(UP_NAMES[p_mip]);
        if (!base || !low || !out) return;

        struct Push {
            uint32_t base_index;
            uint32_t low_index;
            uint32_t out_slot;
            uint32_t sampler_index;
            uint32_t out_width;
            uint32_t out_height;
            uint32_t low_width;
            uint32_t low_height;
            float out_scale;
        } pc{};
        pc.base_index = base->bindless_sampled;
        pc.low_index = low->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.sampler_index = linear_sampler.bindless_sampler;
        pc.out_width = out->extent.width;
        pc.out_height = out->extent.height;
        pc.low_width = low->extent.width;
        pc.low_height = low->extent.height;
        pc.out_scale = p_mip == 0 ? 1.0f / (float)mip_count : 1.0f;

        cl.dd->command_bind_pipeline(cl.cmd, up_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Bloom up", (pc.out_width + 7) / 8, (pc.out_height + 7) / 8);
    };
}

Error BloomFeature::create_resources()
{
    using enum Error;
    drivers::DeviceDriverVulkan::SamplerCreateInfo sci{};
    sci.filter = VK_FILTER_LINEAR;
    sci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.name = "bloom";
    linear_sampler = ctx->dd->sampler_create(sci);
    LUMEN_ERR_FAIL_COND_V(linear_sampler.bindless_sampler == UINT32_MAX, FAILED);
    for (uint32_t i = 0; i < MAX_MIPS; i++) _create_down_pass(i);
    for (uint32_t i = 0; i + 1 < MAX_MIPS; i++) _create_up_pass(i);
    return OK;
}

Error BloomFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_POST_BLOOM_DOWN_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/bloom_down.comp" });
    down_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/bloom_down" });
    ctx->dd->shader_free(cs);

    blob = EmbeddedResource::load(L"SHADERS_POST_BLOOM_UP_COMP");
    cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/bloom_up.comp" });
    up_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/bloom_up" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void BloomFeature::destroy_resources()
{
    ctx->dd->pipeline_free(down_pipe);
    ctx->dd->pipeline_free(up_pipe);
    ctx->dd->sampler_free(linear_sampler);
}

void BloomFeature::build(RenderGraph& g)
{
    mip_count = 0;
    if (!enabled || intensity <= 0.0f || !g.image_resource("SceneColor")) return;

    const bool taa = g.image_resource("TAAOutput") != nullptr;
    source = taa ? "TAAOutput" : "SceneColor";
    uint32_t w = taa && ctx->frame->output_width ? ctx->frame->output_width : g.width;
    uint32_t h = taa && ctx->frame->output_height ? ctx->frame->output_height : g.height;
    while (mip_count < MAX_MIPS) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        if (mip_count >= 2 && std::min(w, h) < MIN_MIP_SIZE) break;
        mip_width[mip_count] = w;
        mip_height[mip_count] = h;
        mip_count++;
    }

    for (uint32_t i = 0; i < mip_count; i++) g.add(&down_passes[i]);
    for (uint32_t i = mip_count - 1; i-- > 0;) g.add(&up_passes[i]);
}

}
