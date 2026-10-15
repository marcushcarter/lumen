#include <core/rendering/features/atmosphere/atmosphere_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>

namespace lumen {

static constexpr VkFormat LUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
static constexpr VkImageLayout LUT_READ_LAYOUT = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
static constexpr VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

static drivers::DeviceDriverVulkan::ImageCreateInfo _lut_info(uint32_t p_width, uint32_t p_height, const char* p_name)
{
    drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
    ci.format = LUT_FORMAT;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
    ci.fixed_width = p_width;
    ci.fixed_height = p_height;
    ci.name = p_name;
    return ci;
}

static void _read_lut(RenderGraph::Builder& b, const char* p_name)
{
    b.read_image(p_name, LUT_READ_LAYOUT, CS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

static void _write_lut(RenderGraph::Builder& b, const char* p_name)
{
    b.write_image(p_name, VK_IMAGE_LAYOUT_GENERAL, CS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void AtmosphereFeature::_create_transmittance_pass()
{
    transmittance_pass.name = "AtmosphereTransmittance";
    transmittance_pass.category = PASS_CATEGORY_LIGHTING;
    transmittance_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Atmosphere", CS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        _write_lut(b, TRANSMITTANCE_LUT);
    };
    transmittance_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* out = cl.graph->image(TRANSMITTANCE_LUT);
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
        b.read_buffer("Atmosphere", CS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        _read_lut(b, TRANSMITTANCE_LUT);
        _write_lut(b, MULTISCATTER_LUT);
    };
    multiscatter_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* transmittance = cl.graph->image(TRANSMITTANCE_LUT);
        auto* out = cl.graph->image(MULTISCATTER_LUT);
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
        b.create_image(SKYVIEW_SUN, _lut_info(SKYVIEW_WIDTH, SKYVIEW_HEIGHT, SKYVIEW_SUN));
        b.create_image(SKYVIEW_MOON, _lut_info(SKYVIEW_WIDTH, SKYVIEW_HEIGHT, SKYVIEW_MOON));
        b.read_buffer("Atmosphere", CS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        _read_lut(b, TRANSMITTANCE_LUT);
        _read_lut(b, MULTISCATTER_LUT);
        _write_lut(b, SKYVIEW_SUN);
        _write_lut(b, SKYVIEW_MOON);
    };
    skyview_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* transmittance = cl.graph->image(TRANSMITTANCE_LUT);
        auto* multiscatter = cl.graph->image(MULTISCATTER_LUT);
        auto* sun = cl.graph->image(SKYVIEW_SUN);
        auto* moon = cl.graph->image(SKYVIEW_MOON);
        if (!atmosphere || !transmittance || !multiscatter || !sun || !moon) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            uint32_t transmittance_index;
            uint32_t multiscatter_index;
            uint32_t sampler_index;
            uint32_t sun_slot;
            uint32_t moon_slot;
            uint32_t width;
            uint32_t height;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.multiscatter_index = multiscatter->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;
        pc.sun_slot = sun->bindless_storage;
        pc.moon_slot = moon->bindless_storage;
        pc.width = SKYVIEW_WIDTH;
        pc.height = SKYVIEW_HEIGHT;

        cl.dd->command_bind_pipeline(cl.cmd, skyview_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sky-view LUT", (SKYVIEW_WIDTH + 7) / 8, (SKYVIEW_HEIGHT + 7) / 8);
    };
}

void AtmosphereFeature::_create_aerial_pass()
{
    aerial_pass.name = "AtmosphereAerialPerspective";
    aerial_pass.category = PASS_CATEGORY_LIGHTING;
    aerial_pass.setup = [](RenderGraph::Builder& b) {
        b.create_image(AERIAL_PERSPECTIVE, _lut_info(AERIAL_SIZE * AERIAL_SLICES, AERIAL_SIZE, AERIAL_PERSPECTIVE));
        b.read_buffer("Camera", CS, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Atmosphere", CS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        _read_lut(b, TRANSMITTANCE_LUT);
        _read_lut(b, MULTISCATTER_LUT);
        _write_lut(b, AERIAL_PERSPECTIVE);
    };
    aerial_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* camera = cl.graph->buffer("Camera");
        auto* transmittance = cl.graph->image(TRANSMITTANCE_LUT);
        auto* multiscatter = cl.graph->image(MULTISCATTER_LUT);
        auto* out = cl.graph->image(AERIAL_PERSPECTIVE);
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
        b.create_buffer(SKY_LIGHTING, ci);
        b.read_buffer("Atmosphere", CS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        _read_lut(b, SKYVIEW_SUN);
        _read_lut(b, SKYVIEW_MOON);
        b.write_buffer(SKY_LIGHTING, CS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    sky_lighting_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* out = cl.graph->buffer(SKY_LIGHTING);
        auto* sun = cl.graph->image(SKYVIEW_SUN);
        auto* moon = cl.graph->image(SKYVIEW_MOON);
        if (!atmosphere || !out || !sun || !moon) return;

        struct Push {
            VkDeviceAddress atmosphere_addr;
            VkDeviceAddress out_addr;
            uint32_t skyview_sun_index;
            uint32_t skyview_moon_index;
            uint32_t sampler_index;
        } pc{};
        pc.atmosphere_addr = atmosphere->device_address;
        pc.out_addr = out->device_address;
        pc.skyview_sun_index = sun->bindless_sampled;
        pc.skyview_moon_index = moon->bindless_sampled;
        pc.sampler_index = lut_sampler.bindless_sampler;

        cl.dd->command_bind_pipeline(cl.cmd, sky_lighting_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sky SH", 1);
    };
}

drivers::DeviceDriverVulkan::Image AtmosphereFeature::_create_lut(uint32_t p_width, uint32_t p_height, const char* p_name)
{
    drivers::DeviceDriverVulkan::ImageCreateInfo ci = _lut_info(p_width, p_height, p_name);
    ci.pool = ctx->dd->image_persistent_pool;
    return ctx->dd->image_create_dedicated(ci, { p_width, p_height });
}

Error AtmosphereFeature::create_resources()
{
    using enum Error;

    transmittance_lut = _create_lut(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, "atmosphere_transmittance");
    multiscatter_lut = _create_lut(MULTISCATTER_SIZE, MULTISCATTER_SIZE, "atmosphere_multiscatter");
    LUMEN_ERR_FAIL_COND_V(!transmittance_lut.image || !multiscatter_lut.image, FAILED);
    built_lut_version = UINT64_MAX;

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

drivers::DeviceDriverVulkan::Pipeline AtmosphereFeature::_compute_pipeline(const wchar_t* p_resource, const char* p_name)
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    drivers::DeviceDriverVulkan::Pipeline pipe = ctx->dd->compute_pipeline_create({ cs, p_name });
    ctx->dd->shader_free(cs);
    return pipe;
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
    lut_sampler = {};
    transmittance_lut = {};
    multiscatter_lut = {};
    built_lut_version = UINT64_MAX;
}

void AtmosphereFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Depth")) return;

    g.import_image(TRANSMITTANCE_LUT, &transmittance_lut, LUT_READ_LAYOUT, CS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    g.import_image(MULTISCATTER_LUT, &multiscatter_lut, LUT_READ_LAYOUT, CS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
    if (built_lut_version != ctx->frame->atmosphere_lut_version) {
        g.add(&transmittance_pass);
        g.add(&multiscatter_pass);
    }

    g.add(&skyview_pass);
    g.add(&aerial_pass);
    g.add(&sky_lighting_pass);
}

}