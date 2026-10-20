#include <core/rendering/features/post/post_process_feature.h>
#include <core/rendering/features/post/taa_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <cmath>

namespace lumen {

void PostProcessFeature::_create_composite_pass()
{
    composite_pass.name = "Composite";
    composite_pass.category = PASS_CATEGORY_POST;
    composite_pass.setup = [this](RenderGraph::Builder& b) {
        _source = b.graph->image_resource("TAAOutput") ? "TAAOutput" : "SceneColor";
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "DisplayColor";
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (ctx->frame->output_width && ctx->frame->output_height) {
            ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
            ci.fixed_width = ctx->frame->output_width;
            ci.fixed_height = ctx->frame->output_height;
        }
        b.create_image("DisplayColor", ci);
        b.read_image(_source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("DisplayColor", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (b.graph->buffer_resource("Exposure")) b.read_buffer("Exposure", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    composite_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* src = cl.graph->image(_source);
        auto* out = cl.graph->image("DisplayColor");
        if (!src || !out) return;
        auto* exposure = cl.graph->buffer("Exposure");

        struct Push {
            VkDeviceAddress exposure_addr;
            uint32_t src_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            uint32_t tonemapper;
            float exposure_scale;
            uint32_t src_width;
            uint32_t src_height;
            uint32_t has_exposure;
        } pc{};
        pc.exposure_addr = exposure ? exposure->device_address : 0;
        pc.has_exposure = exposure ? 1u : 0u;
        pc.src_index = src->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.tonemapper = (uint32_t)tonemapper;
        pc.exposure_scale = std::exp2(exposure_compensation_ev);
        pc.src_width = src->extent.width;
        pc.src_height = src->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, composite_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Composite", (pc.width + 7) / 8, (pc.height + 7) / 8);
    };
}

Error PostProcessFeature::create_resources()
{
    _create_composite_pass();
    return Error::OK;
}

Error PostProcessFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_POST_COMPOSITE_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/composite.comp" });
    composite_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/composite" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void PostProcessFeature::destroy_resources()
{
    ctx->dd->pipeline_free(composite_pipe);
}

void PostProcessFeature::build(RenderGraph& g)
{
    if (!g.image_resource("SceneColor") || g.image_resource("DisplayColor")) return;
    g.add(&composite_pass);
}

}