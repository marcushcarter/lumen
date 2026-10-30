#include <core/rendering/features/post/lens_flare_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void LensFlareFeature::_create_init_pass()
{
    init_pass.name = "LensFlareInit";
    init_pass.category = PASS_CATEGORY_POST;
    init_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("LensFlare", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    init_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* buf = cl.graph->buffer("LensFlare");
        if (!buf) return;
        static const uint32_t zeros[BUFFER_SIZE / sizeof(uint32_t)] = {};
        cl.update_buffer("Lens flare init", *buf, zeros, BUFFER_SIZE, 0);
        initialized = true;
    };
}

void LensFlareFeature::_create_visibility_pass()
{
    visibility_pass.name = "LensFlareVisibility";
    visibility_pass.category = PASS_CATEGORY_POST;
    visibility_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("LensFlare", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("TransmittanceLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    };
    visibility_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* flare = cl.graph->buffer("LensFlare");
        auto* camera = cl.graph->buffer("Camera");
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* depth = cl.graph->image("G_Depth");
        auto* transmittance = cl.graph->image("TransmittanceLut");
        if (!flare || !camera || !atmosphere || !depth || !transmittance || !lut_sampler) return;

        const LensFlareSettings& s = ctx->frame->flare;
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress atmosphere_addr;
            VkDeviceAddress flare_addr;
            uint32_t depth_index;
            uint32_t transmittance_index;
            uint32_t sampler_index;
            uint32_t depth_width;
            uint32_t depth_height;
            float intensity;
            float glow;
            float rays;
            float ray_length;
            float ghosts;
            float blend;
            uint32_t _pad;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.atmosphere_addr = atmosphere->device_address;
        pc.flare_addr = flare->device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.sampler_index = *lut_sampler;
        pc.depth_width = depth->extent.width;
        pc.depth_height = depth->extent.height;
        pc.intensity = s.intensity;
        pc.glow = s.glow;
        pc.rays = s.rays;
        pc.ray_length = std::max(s.ray_length, 1e-3f);
        pc.ghosts = s.ghosts;
        pc.blend = reset ? 1.0f : 1.0f - std::exp(-dt / VISIBILITY_SMOOTHING_S);

        cl.dd->command_bind_pipeline(cl.cmd, visibility_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Lens flare visibility", 1);
        was_enabled = true;
    };
}

Error LensFlareFeature::create_resources()
{
    using enum Error;
    drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
    ci.size = BUFFER_SIZE;
    ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ci.device_local = true;
    ci.name = "LensFlare";
    flare_buffer = ctx->dd->buffer_create(ci);
    LUMEN_ERR_FAIL_COND_V(!flare_buffer.buffer, FAILED);
    initialized = false;
    reset = true;
    _create_init_pass();
    _create_visibility_pass();
    return OK;
}

Error LensFlareFeature::create_pipelines()
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_POST_LENS_FLARE_VISIBILITY_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "post/lens_flare_visibility.comp" });
    visibility_pipe = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = "post/lens_flare_visibility" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void LensFlareFeature::destroy_resources()
{
    ctx->dd->pipeline_free(visibility_pipe);
    if (flare_buffer.buffer) ctx->dd->buffer_free(flare_buffer);
    flare_buffer = {};
}

void LensFlareFeature::build(RenderGraph& g)
{
    const auto now = std::chrono::steady_clock::now();
    dt = last_time.time_since_epoch().count() ? std::clamp(std::chrono::duration<float>(now - last_time).count(), 0.0f, MAX_DT) : 0.0f;
    last_time = now;

    const bool active = enabled && flare_buffer.buffer && ctx->frame->flare.intensity > 0.0f
        && g.image_resource("G_Depth") && g.image_resource("TransmittanceLut") && g.buffer_resource("Camera") && g.buffer_resource("Atmosphere");
    if (!active) {
        was_enabled = false;
        return;
    }
    reset = !was_enabled || ctx->frame->taa_reset;
    was_enabled = false;

    g.import_buffer("LensFlare", &flare_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    if (!initialized) g.add(&init_pass);
    g.add(&visibility_pass);
}

}
