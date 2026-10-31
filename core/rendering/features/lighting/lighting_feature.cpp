#include <core/rendering/features/lighting/lighting_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>

namespace lumen {

void LightingFeature::_create_lighting_pass()
{
    lighting_pass.name = "DeferredLighting";
    lighting_pass.category = PASS_CATEGORY_LIGHTING;
    lighting_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "SceneColor";
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("SceneColor", ci);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Normal", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("ShadingTiles", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Lights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_image("SceneColor", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    lighting_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* normal = cl.graph->image("G_Normal");
        auto* camera = cl.graph->buffer("Camera");
        auto* lights = cl.graph->buffer("Lights");
        auto* tiles = cl.graph->buffer("ShadingTiles");
        auto* out = cl.graph->image("SceneColor");
        if (!depth || !normal || !camera || !lights || !tiles || !out) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress lights_addr;
            VkDeviceAddress tiles_addr;
            vec3 sky_radiance;
            uint32_t depth_index;
            uint32_t normal_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            uint32_t directional_count;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.lights_addr = lights->device_address;
        pc.tiles_addr = tiles->device_address;
        pc.sky_radiance = ctx->frame->sky_radiance;
        pc.depth_index = depth->bindless_sampled;
        pc.normal_index = normal->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.directional_count = ctx->frame->directional_count;

        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        static const char* NAMES[SHADING_TILE_TYPES] = { "Deferred lighting (sky)", "Deferred lighting (lit)", "Deferred lighting (mixed)" };
        for (uint32_t t = 0; t < SHADING_TILE_TYPES; t++) {
            cl.dd->command_bind_pipeline(cl.cmd, lighting_pipes[t]);
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
    static const char* NAMES[SHADING_TILE_TYPES] = { "lighting/deferred_lighting (sky)", "lighting/deferred_lighting (lit)", "lighting/deferred_lighting (mixed)" };
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_LIGHTING_DEFERRED_LIGHTING_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "lighting/deferred_lighting.comp" });
    const VkSpecializationMapEntry entry{ 0, 0, sizeof(uint32_t) };
    for (uint32_t t = 0; t < SHADING_TILE_TYPES; t++) {
        const VkSpecializationInfo spec{ 1, &entry, sizeof(uint32_t), &t };
        lighting_pipes[t] = ctx->dd->compute_pipeline_create({ .compute_shader = cs, .name = NAMES[t], .specialization = &spec });
    }
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void LightingFeature::destroy_resources()
{
    for (drivers::DeviceDriverVulkan::Pipeline& p : lighting_pipes) {
        ctx->dd->pipeline_free(p);
        p = {};
    }
}

void LightingFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Normal")) return;
    g.add(&lighting_pass);
}

}
