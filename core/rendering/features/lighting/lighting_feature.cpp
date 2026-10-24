#include <core/rendering/features/lighting/lighting_feature.h>
#include <core/rendering/features/atmosphere/atmosphere_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <core/world/components.h>
#include <algorithm>
#include <cmath>

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
        b.read_image("G_Emissive", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("ShadingTiles", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Lights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Atmosphere", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SkyLighting", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_image("TransmittanceLut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("AerialPerspective", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("SkyView", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
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
        auto* emissive = cl.graph->image("G_Emissive");
        auto* camera = cl.graph->buffer("Camera");
        auto* lights = cl.graph->buffer("Lights");
        auto* cull = cl.graph->buffer("LightCullData");
        auto* sorted = cl.graph->buffer("SortedLights");
        auto* masks = cl.graph->buffer("LightTileMasks");
        auto* out = cl.graph->image("SceneColor");
        auto* atmosphere = cl.graph->buffer("Atmosphere");
        auto* sky_lighting = cl.graph->buffer("SkyLighting");
        auto* transmittance = cl.graph->image("TransmittanceLut");
        auto* aerial = cl.graph->image("AerialPerspective");
        auto* skyview = cl.graph->image("SkyView");
        auto* tiles = cl.graph->buffer("ShadingTiles");
        if (!tiles) return;
        if (!depth || !albedo || !normal || !material || !emissive || !camera || !lights || !out) return;
        if (!atmosphere || !sky_lighting || !transmittance || !aerial || !skyview || !lut_sampler) return;
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
            VkDeviceAddress tiles_addr;
            uint32_t depth_index;
            uint32_t albedo_index;
            uint32_t normal_index;
            uint32_t material_index;
            uint32_t emissive_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            uint32_t directional_count;
            uint32_t transmittance_index;
            uint32_t aerial_index;
            uint32_t sampler_index;
            uint32_t skyview_index;
            float sun_reject_cos;
            float moon_reject_cos;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.lights_addr = lights->device_address;
        pc.cull_addr = local_lights ? cull->device_address : 0;
        pc.sorted_addr = local_lights ? sorted->device_address : 0;
        pc.masks_addr = masks ? masks->device_address : 0;
        pc.atmosphere_addr = atmosphere->device_address;
        pc.sky_lighting_addr = sky_lighting->device_address;
        pc.tiles_addr = tiles->device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.albedo_index = albedo->bindless_sampled;
        pc.normal_index = normal->bindless_sampled;
        pc.material_index = material->bindless_sampled;
        pc.emissive_index = emissive->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.directional_count = (ctx->frame->directional_count & 0xFFFFu) | (local_lights ? LIGHTING_FLAG_LOCAL_LIGHTS : 0u);
        pc.transmittance_index = transmittance->bindless_sampled;
        pc.aerial_index = aerial->bindless_sampled;
        pc.sampler_index = *lut_sampler;
        pc.skyview_index = skyview->bindless_sampled;
        const float pixel_angle = 2.0f * ctx->frame->camera.tan_half_fov_y / (float)pc.height;
        pc.sun_reject_cos = std::cos(std::min(AtmosphereComponent::SUN_ANGULAR_RADIUS + 2.0f * pixel_angle, 3.14159265f));
        pc.moon_reject_cos = std::cos(std::min(AtmosphereComponent::MOON_ANGULAR_RADIUS + 2.0f * pixel_angle, 3.14159265f));

        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        static const char* NAMES[SHADING_TILE_TYPES] = { "Deferred lighting (sky)", "Deferred lighting (lit)", "Deferred lighting (unlit)", "Deferred lighting (mixed)" };
        for (uint32_t t = 0; t < SHADING_TILE_TYPES; t++) {
            cl.dd->command_bind_pipeline(cl.cmd, lighting_pipes[t][mode]);
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.dispatch_indirect(NAMES[t], *tiles, t * SHADING_TILES_ARGS_STRIDE);
        }
    };
}

Error LightingFeature::create_resources()
{
    _create_lighting_pass();
    return Error::OK;
}

Error LightingFeature::create_pipelines()
{
    static const char* NAMES[SHADING_TILE_TYPES][LIGHT_MODE_COUNT] = {
        { "lighting/deferred_lighting (sky)", "lighting/deferred_lighting (sky)", "lighting/deferred_lighting (sky)" },
        { "lighting/deferred_lighting (lit, no local)", "lighting/deferred_lighting (lit, zbin)", "lighting/deferred_lighting (lit, tiled)" },
        { "lighting/deferred_lighting (unlit)", "lighting/deferred_lighting (unlit)", "lighting/deferred_lighting (unlit)" },
        { "lighting/deferred_lighting (mixed, no local)", "lighting/deferred_lighting (mixed, zbin)", "lighting/deferred_lighting (mixed, tiled)" },
    };
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_LIGHTING_DEFERRED_LIGHTING_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "lighting/deferred_lighting.comp" });
    const VkSpecializationMapEntry entries[2] = { { 0, 0, sizeof(uint32_t) }, { 1, sizeof(uint32_t), sizeof(uint32_t) } };
    for (uint32_t t = 0; t < SHADING_TILE_TYPES; t++) {
        for (uint32_t m = 0; m < LIGHT_MODE_COUNT; m++) {
            if ((t == SHADING_TILE_SKY || t == SHADING_TILE_UNLIT) && m != LIGHT_MODE_NONE) {
                lighting_pipes[t][m] = lighting_pipes[t][LIGHT_MODE_NONE];
                continue;
            }
            const uint32_t values[2] = { m, t };
            const VkSpecializationInfo spec{ 2, entries, sizeof(values), values };
            lighting_pipes[t][m] = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = NAMES[t][m], .specialization = &spec });
        }
    }
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void LightingFeature::destroy_resources()
{
    for (uint32_t t = 0; t < SHADING_TILE_TYPES; t++) {
        for (uint32_t m = 0; m < LIGHT_MODE_COUNT; m++) {
            if ((t != SHADING_TILE_SKY && t != SHADING_TILE_UNLIT) || m == LIGHT_MODE_NONE) ctx->dd->pipeline_free(lighting_pipes[t][m]);
            lighting_pipes[t][m] = {};
        }
    }
}

void LightingFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Albedo")) return;
    g.add(&lighting_pass);
}

}