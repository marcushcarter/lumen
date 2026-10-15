#include <core/rendering/features/lighting/lighting_feature.h>
#include <core/rendering/features/atmosphere/atmosphere_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>

namespace lumen {

static constexpr uint32_t LIGHTING_FLAG_LOCAL_LIGHTS = 1u << 16;

void LightingFeature::_create_lighting_pass()
{
    lighting_pass.name = "DeferredLighting";
    lighting_pass.category = PASS_CATEGORY_LIGHTING;
    lighting_pass.setup = [](RenderGraph::Builder& b) {
        const bool local_lights = b.graph->buffer_resource("LightCullData") != nullptr;
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "SceneColor";
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("SceneColor", ci);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Albedo", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Normal", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Material", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Lights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(AtmosphereFeature::SKY_LIGHTING, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image(AtmosphereFeature::TRANSMITTANCE_LUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image(AtmosphereFeature::SKYVIEW_SUN, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image(AtmosphereFeature::SKYVIEW_MOON, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image(AtmosphereFeature::AERIAL_PERSPECTIVE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        if (local_lights) {
            b.read_buffer("LightCullData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.read_buffer("SortedLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            if (b.graph->buffer_resource("LightTileMasks")) b.read_buffer("LightTileMasks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }
        b.write_image("SceneColor", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    lighting_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* albedo = cl.graph->image("G_Albedo");
        auto* normal = cl.graph->image("G_Normal");
        auto* material = cl.graph->image("G_Material");
        auto* camera = cl.graph->buffer("Camera");
        auto* lights = cl.graph->buffer("Lights");
        auto* cull = cl.graph->buffer("LightCullData");
        auto* sorted = cl.graph->buffer("SortedLights");
        auto* masks = cl.graph->buffer("LightTileMasks");
        auto* out = cl.graph->image("SceneColor");
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* sky_lighting = cl.graph->buffer(AtmosphereFeature::SKY_LIGHTING);
        auto* transmittance = cl.graph->image(AtmosphereFeature::TRANSMITTANCE_LUT);
        auto* skyview_sun = cl.graph->image(AtmosphereFeature::SKYVIEW_SUN);
        auto* skyview_moon = cl.graph->image(AtmosphereFeature::SKYVIEW_MOON);
        auto* aerial = cl.graph->image(AtmosphereFeature::AERIAL_PERSPECTIVE);
        if (!depth || !albedo || !normal || !material || !camera || !lights || !out) return;
        if (!atmosphere || !sky_lighting || !transmittance || !skyview_sun || !skyview_moon || !aerial || !lut_sampler) return;
        const bool local_lights = cull && sorted;
        const uint32_t mode = !local_lights ? LIGHT_MODE_NONE : (masks ? LIGHT_MODE_TILED : LIGHT_MODE_ZBIN);

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress lights_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress sorted_addr;
            VkDeviceAddress masks_addr;
            VkDeviceAddress atmosphere_addr;
            VkDeviceAddress sky_lighting_addr;
            uint32_t depth_index;
            uint32_t albedo_index;
            uint32_t normal_index;
            uint32_t material_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            uint32_t directional_count;
            uint32_t transmittance_index;
            uint32_t skyview_sun_index;
            uint32_t skyview_moon_index;
            uint32_t aerial_index;
            uint32_t sampler_index;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.lights_addr = lights->device_address;
        pc.cull_addr = local_lights ? cull->device_address : 0;
        pc.sorted_addr = local_lights ? sorted->device_address : 0;
        pc.masks_addr = masks ? masks->device_address : 0;
        pc.atmosphere_addr = atmosphere->device_address;
        pc.sky_lighting_addr = sky_lighting->device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.albedo_index = albedo->bindless_sampled;
        pc.normal_index = normal->bindless_sampled;
        pc.material_index = material->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.directional_count = (ctx->frame->directional_count & 0xFFFFu) | (local_lights ? LIGHTING_FLAG_LOCAL_LIGHTS : 0u);
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.skyview_sun_index = skyview_sun->bindless_sampled;
        pc.skyview_moon_index = skyview_moon->bindless_sampled;
        pc.aerial_index = aerial->bindless_sampled;
        pc.sampler_index = *lut_sampler;

        cl.dd->command_bind_pipeline(cl.cmd, lighting_pipes[mode]);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Deferred lighting", (pc.width + 7) / 8, (pc.height + 7) / 8);
    };
}

Error LightingFeature::create_resources()
{
    _create_lighting_pass();
    return Error::OK;
}

Error LightingFeature::create_pipelines()
{
    static const char* NAMES[LIGHT_MODE_COUNT] = { "lighting/deferred_lighting (no local)", "lighting/deferred_lighting (zbin)", "lighting/deferred_lighting (tiled)" };
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_LIGHTING_DEFERRED_LIGHTING_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "lighting/deferred_lighting.comp" });
    for (uint32_t m = 0; m < LIGHT_MODE_COUNT; m++) {
        const VkSpecializationMapEntry entry{ 0, 0, sizeof(uint32_t) };
        const VkSpecializationInfo spec{ 1, &entry, sizeof(uint32_t), &m };
        lighting_pipes[m] = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = NAMES[m], .specialization = &spec });
    }
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void LightingFeature::destroy_resources()
{
    for (drivers::DeviceDriverVulkan::Pipeline& p : lighting_pipes) ctx->dd->pipeline_free(p);
}

void LightingFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Albedo")) return;
    g.add(&lighting_pass);
}

}