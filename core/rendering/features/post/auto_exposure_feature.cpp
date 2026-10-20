#include <core/rendering/features/post/auto_exposure_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <algorithm>

namespace lumen {

void AutoExposureFeature::_create_init_pass()
{
    init_pass.name = "ExposureInit";
    init_pass.category = PASS_CATEGORY_POST;
    init_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("Exposure", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    init_pass.execute = [](RenderGraph::CommandList& cl) {
        auto* buf = cl.graph->buffer("Exposure");
        if (!buf) return;
        static const uint32_t zeros[BUFFER_SIZE / sizeof(uint32_t)] = {};
        cl.update_buffer("Exposure init", *buf, zeros, BUFFER_SIZE, 0);
    };
}

void AutoExposureFeature::_create_histogram_pass()
{
    histogram_pass.name = "ExposureHistogram";
    histogram_pass.category = PASS_CATEGORY_POST;
    histogram_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("SceneColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_buffer("Exposure", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    histogram_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* color = cl.graph->image("SceneColor");
        auto* buf = cl.graph->buffer("Exposure");
        if (!color || !buf) return;

        struct Push {
            VkDeviceAddress exposure_addr;
            uint32_t src_index;
            uint32_t width;
            uint32_t height;
            uint32_t _pad;
        } pc{};
        pc.exposure_addr = buf->device_address;
        pc.src_index = color->bindless_sampled;
        pc.width = color->extent.width;
        pc.height = color->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, histogram_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Exposure histogram", (pc.width + 31) / 32, (pc.height + 31) / 32);
    };
}

void AutoExposureFeature::_create_reduce_pass()
{
    reduce_pass.name = "ExposureReduce";
    reduce_pass.category = PASS_CATEGORY_POST;
    reduce_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("Exposure", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    reduce_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* buf = cl.graph->buffer("Exposure");
        if (!buf) return;

        struct Push {
            VkDeviceAddress exposure_addr;
            float dt;
            float min_ev;
            float max_ev;
            float low_percentile;
            float high_percentile;
            float speed_brighten;
            float speed_darken;
            uint32_t reset;
        } pc{};
        pc.exposure_addr = buf->device_address;
        pc.dt = _dt;
        pc.min_ev = std::min(min_ev, max_ev);
        const float sun_y = ctx->frame->atmosphere.sun_direction.y;
        const float day = std::clamp((sun_y - NIGHT_SUN_Y) / (DAY_SUN_Y - NIGHT_SUN_Y), 0.0f, 1.0f);
        pc.max_ev = std::max(pc.min_ev, std::max(min_ev, max_ev) * day * day * (3.0f - 2.0f * day));
        pc.low_percentile = std::clamp(low_percentile, 0.0f, 1.0f);
        pc.high_percentile = std::clamp(high_percentile, pc.low_percentile, 1.0f);
        pc.speed_brighten = speed_brighten;
        pc.speed_darken = speed_darken;
        pc.reset = _reset ? 1u : 0u;

        cl.dd->command_bind_pipeline(cl.cmd, reduce_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Exposure reduce", 1);
    };
}

Error AutoExposureFeature::create_resources()
{
    using enum Error;
    drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
    ci.size = BUFFER_SIZE;
    ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ci.device_local = true;
    ci.name = "Exposure";
    exposure_buffer = ctx->dd->buffer_create(ci);
    LUMEN_ERR_FAIL_COND_V(!exposure_buffer.buffer, FAILED);
    _initialized = false;
    _reset = true;
    _create_init_pass();
    _create_histogram_pass();
    _create_reduce_pass();
    return OK;
}

Error AutoExposureFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_POST_EXPOSURE_HISTOGRAM_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/exposure_histogram.comp" });
    histogram_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/exposure_histogram" });
    ctx->dd->shader_free(cs);

    blob = EmbeddedResource::load(L"SHADERS_POST_EXPOSURE_REDUCE_COMP");
    cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/exposure_reduce.comp" });
    reduce_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/exposure_reduce" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void AutoExposureFeature::destroy_resources()
{
    ctx->dd->pipeline_free(histogram_pipe);
    ctx->dd->pipeline_free(reduce_pipe);
    if (exposure_buffer.buffer) ctx->dd->buffer_free(exposure_buffer);
    exposure_buffer = {};
}

void AutoExposureFeature::build(RenderGraph& g)
{
    const auto now = std::chrono::steady_clock::now();
    _dt = _last_time.time_since_epoch().count() ? std::clamp(std::chrono::duration<float>(now - _last_time).count(), 0.0f, MAX_DT) : 0.0f;
    _last_time = now;

    if (!enabled || !exposure_buffer.buffer || !g.image_resource("SceneColor")) {
        _was_enabled = false;
        return;
    }
    if (!_was_enabled || ctx->frame->taa_reset) _reset = true;
    _was_enabled = true;

    g.import_buffer("Exposure", &exposure_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    if (!_initialized) {
        g.add(&init_pass);
        _initialized = true;
    }
    g.add(&histogram_pass);
    g.add(&reduce_pass);
    _reset = false;
}

}