#include <core/rendering/features/post/taa_feature.h>
#include <core/rendering/features/post/post_process_feature.h>
#include <core/rendering/features/post/bloom_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void TAAFeature::_create_resolve_pass()
{
    resolve_pass.name = "TAA";
    resolve_pass.category = PASS_CATEGORY_POST;
    resolve_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_image("SceneColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Motion", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("TAAHistory", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("TAAOutput", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* color = cl.graph->image("SceneColor");
        auto* motion = cl.graph->image("G_Motion");
        auto* depth = cl.graph->image("G_Depth");
        auto* prev = cl.graph->image("TAAHistory");
        auto* out = cl.graph->image("TAAOutput");
        if (!color || !motion || !depth || !prev || !out) return;

        struct Push {
            uint32_t color_index;
            uint32_t motion_index;
            uint32_t depth_index;
            uint32_t history_index;
            uint32_t out_slot;
            uint32_t sampler_index;
            uint32_t in_width;
            uint32_t in_height;
            uint32_t out_width;
            uint32_t out_height;
            float jitter_px[2];
            uint32_t reset;
        } pc{};
        pc.color_index = color->bindless_sampled;
        pc.motion_index = motion->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.history_index = prev->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.sampler_index = linear_sampler.bindless_sampler;
        pc.in_width = color->extent.width;
        pc.in_height = color->extent.height;
        pc.out_width = out->extent.width;
        pc.out_height = out->extent.height;
        pc.jitter_px[0] = ctx->frame->camera.jitter.x * 0.5f * (float)pc.in_width;
        pc.jitter_px[1] = ctx->frame->camera.jitter.y * 0.5f * (float)pc.in_height;
        pc.reset = _reset ? 1u : 0u;

        cl.dd->command_bind_pipeline(cl.cmd, resolve_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("TAA", (pc.out_width + 15) / 16, (pc.out_height + 7) / 8);
    };
}

void TAAFeature::_create_sharpen_pass()
{
    sharpen_pass.name = "TAASharpen";
    sharpen_pass.category = PASS_CATEGORY_POST;
    sharpen_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo display_ci{};
        display_ci.name = "DisplayColor";
        display_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        display_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        display_ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
        display_ci.fixed_width = history[0].extent.width;
        display_ci.fixed_height = history[0].extent.height;
        b.create_image("DisplayColor", display_ci);
        b.write_image("DisplayColor", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.read_image("TAAOutput", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("SceneColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        if (b.graph->image_resource("Bloom")) b.read_image("Bloom", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        if (b.graph->buffer_resource("LensFlare")) b.read_buffer("LensFlare", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (b.graph->buffer_resource("Exposure")) b.read_buffer("Exposure", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    sharpen_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* src = cl.graph->image("TAAOutput");
        auto* color = cl.graph->image("SceneColor");
        auto* display = cl.graph->image("DisplayColor");
        if (!src || !color || !display) return;
        auto* exposure = cl.graph->buffer("Exposure");
        auto* flare = cl.graph->buffer("LensFlare");
        auto* bloom_img = cl.graph->image("Bloom");
        const BloomFeature* bloom_cfg = post ? post->bloom : nullptr;

        struct Push {
            VkDeviceAddress exposure_addr;
            VkDeviceAddress flare_addr;
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            float sharpness;
            uint32_t tonemapper;
            float exposure_scale;
            uint32_t has_exposure;
            uint32_t bloom_index;
            uint32_t bloom_sampler;
            float bloom_intensity;
            uint32_t has_flare;
        } pc{};
        pc.exposure_addr = exposure ? exposure->device_address : 0;
        pc.has_exposure = exposure ? 1u : 0u;
        pc.flare_addr = flare ? flare->device_address : 0;
        pc.has_flare = flare ? 1u : 0u;
        if (bloom_img && bloom_cfg) {
            pc.bloom_index = bloom_img->bindless_sampled;
            pc.bloom_sampler = bloom_cfg->linear_sampler.bindless_sampler;
            pc.bloom_intensity = bloom_cfg->intensity;
        }
        pc.src_index = src->bindless_sampled;
        pc.out_slot = display->bindless_storage;
        pc.width = display->extent.width;
        pc.height = display->extent.height;
        pc.sharpness = std::clamp(_base_sharpness((float)color->extent.width / (float)src->extent.width) * sharpness, 0.0f, 1.0f);
        pc.tonemapper = post ? (uint32_t)post->tonemapper : 0u;
        pc.exposure_scale = post ? std::exp2(post->exposure_compensation_ev) : 1.0f;

        cl.dd->command_bind_pipeline(cl.cmd, sharpen_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("TAA sharpen", (pc.width + 7) / 8, (pc.height + 7) / 8);
    };
}

float TAAFeature::_base_sharpness(float p_ratio) const
{
    if (p_ratio <= 1.0f) return std::clamp(0.5f + (1.0f - p_ratio) * 0.6f, 0.5f, 0.8f);
    return std::max(0.0f, 0.5f * (1.41421356f - p_ratio) / 0.41421356f);
}

void TAAFeature::_ensure_history(uint32_t p_width, uint32_t p_height)
{
    if (history[0].image && history[0].extent.width == p_width && history[0].extent.height == p_height) return;
    for (drivers::DeviceDriverVulkan::Image& h : history) {
        if (h.image) _retired.push_back({ std::move(h), _serial });
        h = {};
    }

    drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
    ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
    ci.fixed_width = p_width;
    ci.fixed_height = p_height;
    ci.pool = ctx->dd->image_persistent_pool;
    ci.name = "taa_history";
    for (drivers::DeviceDriverVulkan::Image& h : history) h = ctx->dd->image_create_dedicated(ci, { p_width, p_height });
    history_valid = false;
}

void TAAFeature::_collect(bool p_all)
{
    const uint64_t frames = ctx->graph->frame_count;
    for (size_t i = 0; i < _retired.size();) {
        if (p_all || _serial >= _retired[i].serial + frames + 1) {
            ctx->dd->image_free(_retired[i].image);
            _retired[i] = std::move(_retired.back());
            _retired.pop_back();
        } else {
            ++i;
        }
    }
}

Error TAAFeature::create_resources()
{
    using enum Error;
    drivers::DeviceDriverVulkan::SamplerCreateInfo sci{};
    sci.filter = VK_FILTER_LINEAR;
    sci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.name = "taa_history";
    linear_sampler = ctx->dd->sampler_create(sci);
    LUMEN_ERR_FAIL_COND_V(linear_sampler.bindless_sampler == UINT32_MAX, FAILED);
    _create_resolve_pass();
    _create_sharpen_pass();
    return OK;
}

Error TAAFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_POST_TAA_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/taa.comp" });
    resolve_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/taa" });
    ctx->dd->shader_free(cs);

    blob = EmbeddedResource::load(L"SHADERS_POST_TAA_SHARPEN_COMP");
    cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/taa_sharpen.comp" });
    sharpen_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/taa_sharpen" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void TAAFeature::destroy_resources()
{
    ctx->dd->pipeline_free(resolve_pipe);
    ctx->dd->pipeline_free(sharpen_pipe);
    ctx->dd->sampler_free(linear_sampler);
    for (drivers::DeviceDriverVulkan::Image& h : history) {
        if (h.image) ctx->dd->image_free(h);
        h = {};
    }
    _collect(true);
}

void TAAFeature::build(RenderGraph& g)
{
    ++_serial;
    _collect(false);
    if (!enabled || suppressed || !g.image_resource("SceneColor") || !g.image_resource("G_Motion") || !g.image_resource("G_Depth")) {
        history_valid = false;
        return;
    }
    const uint32_t w = ctx->frame->output_width ? ctx->frame->output_width : g.width;
    const uint32_t h = ctx->frame->output_height ? ctx->frame->output_height : g.height;
    if (w == 0 || h == 0) return;
    _ensure_history(w, h);

    const uint32_t write = history_index;
    const uint32_t read = history_index ^ 1u;
    g.import_image("TAAOutput", &history[write], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    g.import_image("TAAHistory", &history[read], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    _reset = !history_valid || ctx->frame->taa_reset;
    g.add(&resolve_pass);
    history_valid = true;
    history_index = read;
}

}