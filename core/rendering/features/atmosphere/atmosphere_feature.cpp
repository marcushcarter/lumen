#include <core/rendering/features/atmosphere/atmosphere_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <core/world/world_settings.h>

namespace lumen {

void AtmosphereFeature::_create_transmittance_pass()
{
    transmittance_pass.name = "AtmosphereTransmittance";
    transmittance_pass.category = PASS_CATEGORY_LIGHTING;
    transmittance_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_image("TransmittanceLut", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    transmittance_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* out = cl.graph->image("TransmittanceLut");
        if (!atmosphere || !out) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.out_slot = out->bindless_storage;
        pc.width = TRANSMITTANCE_WIDTH;
        pc.height = TRANSMITTANCE_HEIGHT;

        cl.dd->command_bind_pipeline(cl.cmd, transmittance_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Transmittance LUT", (TRANSMITTANCE_WIDTH + 7) / 8, (TRANSMITTANCE_HEIGHT + 7) / 8);
    };
}

void AtmosphereFeature::_create_multiscatter_pass()
{
    multiscatter_pass.name = "AtmosphereMultiscatter";
    multiscatter_pass.category = PASS_CATEGORY_LIGHTING;
    multiscatter_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("TransmittanceLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("MultiscatterLut", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    multiscatter_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* transmittance = cl.graph->image("TransmittanceLut");
        auto* out = cl.graph->image("MultiscatterLut");
        if (!atmosphere || !transmittance || !out) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            uint32_t transmittance_index;
            uint32_t sampler_index;
            uint32_t out_slot;
            uint32_t size;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;
        pc.out_slot = out->bindless_storage;
        pc.size = MULTISCATTER_SIZE;

        cl.dd->command_bind_pipeline(cl.cmd, multiscatter_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Multiscatter LUT", (MULTISCATTER_SIZE + 7) / 8, (MULTISCATTER_SIZE + 7) / 8);
        built_lut_version = ctx->frame->atmosphere_lut_version;
    };
}

void AtmosphereFeature::_create_skyview_pass()
{
    skyview_pass.name = "AtmosphereSkyView";
    skyview_pass.category = PASS_CATEGORY_LIGHTING;
    skyview_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("TransmittanceLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("MultiscatterLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("SkyView", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    skyview_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* transmittance = cl.graph->image("TransmittanceLut");
        auto* multiscatter = cl.graph->image("MultiscatterLut");
        auto* skyview = cl.graph->image("SkyView");
        if (!atmosphere || !transmittance || !multiscatter || !skyview) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            uint32_t transmittance_index;
            uint32_t multiscatter_index;
            uint32_t sampler_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.multiscatter_index = multiscatter->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;
        pc.out_slot = skyview->bindless_storage;
        pc.width = SKYVIEW_WIDTH;
        pc.height = SKYVIEW_HEIGHT;

        cl.dd->command_bind_pipeline(cl.cmd, skyview_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sky-view LUT", (SKYVIEW_WIDTH + 7) / 8, (SKYVIEW_HEIGHT + 7) / 8);
        skyview_built = skyview_pending;
        skyview_valid = true;
    };
}

bool AtmosphereFeature::_skyview_dirty(const SkyViewKey& p_key) const
{
    if (!skyview_valid || built_lut_version != p_key.lut_version || p_key.lut_version != skyview_built.lut_version) return true;
    if (p_key.mie_g != skyview_built.mie_g) return true;
    if (glm::abs(p_key.altitude_km - skyview_built.altitude_km) > SKYVIEW_REBUILD_ALTITUDE_KM) return true;
    if (glm::dot(p_key.sun_direction, skyview_built.sun_direction) < SKYVIEW_REBUILD_COS) return true;
    if (glm::dot(p_key.moon_direction, skyview_built.moon_direction) < SKYVIEW_REBUILD_COS) return true;
    return false;
}

void AtmosphereFeature::_create_aerial_pass()
{
    aerial_pass.name = "AtmosphereAerialPerspective";
    aerial_pass.category = PASS_CATEGORY_LIGHTING;
    aerial_pass.setup = [](RenderGraph::Builder& b) {
        b.create_image("AerialPerspective", _storage_image_info("AerialPerspective", AERIAL_SIZE * AERIAL_SLICES, AERIAL_SIZE));
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("TransmittanceLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("MultiscatterLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("AerialPerspective", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    aerial_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* camera = cl.graph->buffer("Camera");
        auto* transmittance = cl.graph->image("TransmittanceLut");
        auto* multiscatter = cl.graph->image("MultiscatterLut");
        auto* out = cl.graph->image("AerialPerspective");
        if (!atmosphere || !camera || !transmittance || !multiscatter || !out) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            VkDeviceAddress camera_addr;
            uint32_t transmittance_index;
            uint32_t multiscatter_index;
            uint32_t sampler_index;
            uint32_t out_slot;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.camera_addr = camera->device_address;
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.multiscatter_index = multiscatter->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;
        pc.out_slot = out->bindless_storage;

        cl.dd->command_bind_pipeline(cl.cmd, aerial_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Aerial perspective", (AERIAL_SIZE + 7) / 8, (AERIAL_SIZE + 7) / 8);
    };
}

void AtmosphereFeature::_create_sky_lighting_pass()
{
    sky_lighting_pass.name = "AtmosphereSkyLighting";
    sky_lighting_pass.category = PASS_CATEGORY_LIGHTING;
    sky_lighting_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = SKY_SH_SIZE;
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = true;
        b.create_buffer("SkyLighting", ci);
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("SkyView", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_buffer("SkyLighting", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    sky_lighting_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* out = cl.graph->buffer("SkyLighting");
        auto* skyview = cl.graph->image("SkyView");
        if (!atmosphere || !out || !skyview) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            VkDeviceAddress out_addr;
            uint32_t skyview_index;
            uint32_t sampler_index;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.out_addr = out->device_address;
        pc.skyview_index = skyview->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;

        cl.dd->command_bind_pipeline(cl.cmd, sky_lighting_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sky SH", 1);
    };
}

drivers::DeviceDriverVulkan::Image AtmosphereFeature::_create_lut(uint32_t p_width, uint32_t p_height, const char* p_name)
{
    drivers::DeviceDriverVulkan::ImageCreateInfo ci = _storage_image_info(p_name, p_width, p_height);
    ci.pool = ctx->dd->image_persistent_pool;
    return ctx->dd->image_create_dedicated(ci, { p_width, p_height });
}

Error AtmosphereFeature::create_resources()
{
    using enum Error;

    transmittance_lut = _create_lut(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, "atmosphere_transmittance");
    multiscatter_lut = _create_lut(MULTISCATTER_SIZE, MULTISCATTER_SIZE, "atmosphere_multiscatter");
    LUMEN_ERR_FAIL_COND_V(!transmittance_lut.image || !multiscatter_lut.image, FAILED);
    skyview_lut = _create_lut(SKYVIEW_WIDTH, SKYVIEW_HEIGHT, "atmosphere_skyview");
    LUMEN_ERR_FAIL_COND_V(!skyview_lut.image, FAILED);
    built_lut_version = UINT64_MAX;
    skyview_valid = false;

    drivers::DeviceDriverVulkan::SamplerCreateInfo sci{};
    sci.filter = VK_FILTER_LINEAR;
    sci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.name = "atmosphere_lut";
    lut_sampler = ctx->dd->sampler_create(sci);
    LUMEN_ERR_FAIL_COND_V(lut_sampler.bindless_sampler == UINT32_MAX, FAILED);

    _create_transmittance_pass();
    _create_multiscatter_pass();
    _create_skyview_pass();
    _create_aerial_pass();
    _create_sky_lighting_pass();
    return OK;
}

Error AtmosphereFeature::create_pipelines()
{
    transmittance_pipe = _compute_pipeline(L"SHADERS_ATMOSPHERE_TRANSMITTANCE_LUT_COMP", "atmosphere/transmittance_lut.comp");
    multiscatter_pipe = _compute_pipeline(L"SHADERS_ATMOSPHERE_MULTISCATTER_LUT_COMP", "atmosphere/multiscatter_lut.comp");
    skyview_pipe = _compute_pipeline(L"SHADERS_ATMOSPHERE_SKYVIEW_LUT_COMP", "atmosphere/skyview_lut.comp");
    aerial_pipe = _compute_pipeline(L"SHADERS_ATMOSPHERE_AERIAL_PERSPECTIVE_COMP", "atmosphere/aerial_perspective.comp");
    sky_lighting_pipe = _compute_pipeline(L"SHADERS_ATMOSPHERE_SKY_LIGHTING_COMP", "atmosphere/sky_lighting.comp");
    return Error::OK;
}

void AtmosphereFeature::destroy_resources()
{
    ctx->dd->pipeline_free(transmittance_pipe);
    ctx->dd->pipeline_free(multiscatter_pipe);
    ctx->dd->pipeline_free(skyview_pipe);
    ctx->dd->pipeline_free(aerial_pipe);
    ctx->dd->pipeline_free(sky_lighting_pipe);
    if (lut_sampler.sampler) ctx->dd->sampler_free(lut_sampler);
    if (transmittance_lut.image) ctx->dd->image_free(transmittance_lut);
    if (multiscatter_lut.image) ctx->dd->image_free(multiscatter_lut);
    if (skyview_lut.image) ctx->dd->image_free(skyview_lut);
    lut_sampler = {};
    transmittance_lut = {};
    multiscatter_lut = {};
    skyview_lut = {};
    built_lut_version = UINT64_MAX;
    skyview_valid = false;
}

void AtmosphereFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Depth")) return;
    if (ctx->frame->atmosphere.sky_mode != (uint32_t)SkyMode::ATMOSPHERE) return;

    g.import_image("TransmittanceLut", &transmittance_lut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    g.import_image("MultiscatterLut", &multiscatter_lut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    g.import_image("SkyView", &skyview_lut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    if (built_lut_version != ctx->frame->atmosphere_lut_version) {
        g.add(&transmittance_pass);
        g.add(&multiscatter_pass);
    }

    const GpuAtmosphere& a = ctx->frame->atmosphere;
    SkyViewKey k;
    k.sun_direction = a.sun_direction;
    k.moon_direction = a.moon_direction;
    k.altitude_km = a.altitude_km;
    k.mie_g = a.mie_g;
    k.lut_version = ctx->frame->atmosphere_lut_version;
    skyview_pending = k;

    if (_skyview_dirty(skyview_pending)) g.add(&skyview_pass);
    g.add(&aerial_pass);
    g.add(&sky_lighting_pass);
}

}
