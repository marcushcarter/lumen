#include <core/rendering/features/lighting/lighting_feature.h>
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
        b.read_image("G_Albedo", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Normal", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Material", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.write_image("SceneColor", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    lighting_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* albedo = cl.graph->image("G_Albedo");
        auto* normal = cl.graph->image("G_Normal");
        auto* material = cl.graph->image("G_Material");
        auto* camera = cl.graph->buffer("Camera");
        auto* out = cl.graph->image("SceneColor");

        struct Push {
            VkDeviceAddress camera_addr;
            uint32_t depth_index;
            uint32_t albedo_index;
            uint32_t normal_index;
            uint32_t material_index;
            uint32_t out_slot;
            uint32_t width;
            uint32_t height;
            float ambient_intensity;
            float sun_direction[4];
            float sun_radiance[4];
            float sky_zenith[4];
            float sky_horizon[4];
            float ground_color[4];
        } pc{};
        const glm::vec3 dir = glm::normalize(sun_direction);
        const glm::vec3 radiance = sun_color * sun_intensity;
        pc.camera_addr = camera->device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.albedo_index = albedo->bindless_sampled;
        pc.normal_index = normal->bindless_sampled;
        pc.material_index = material->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        pc.ambient_intensity = ambient_intensity;
        pc.sun_direction[0] = dir.x; pc.sun_direction[1] = dir.y; pc.sun_direction[2] = dir.z;
        pc.sun_radiance[0] = radiance.x; pc.sun_radiance[1] = radiance.y; pc.sun_radiance[2] = radiance.z;
        pc.sky_zenith[0] = sky_zenith.x; pc.sky_zenith[1] = sky_zenith.y; pc.sky_zenith[2] = sky_zenith.z;
        pc.sky_horizon[0] = sky_horizon.x; pc.sky_horizon[1] = sky_horizon.y; pc.sky_horizon[2] = sky_horizon.z;
        pc.ground_color[0] = ground_color.x; pc.ground_color[1] = ground_color.y; pc.ground_color[2] = ground_color.z;

        cl.dd->command_bind_pipeline(cl.cmd, lighting_pipe);
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
    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_LIGHTING_DEFERRED_LIGHTING_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "lighting/deferred_lighting.comp" });
    lighting_pipe = ctx->dd->compute_pipeline_create({ cs, "lighting/deferred_lighting" });
    ctx->dd->shader_free(cs);
    return Error::OK;
}

void LightingFeature::destroy_resources()
{
    ctx->dd->pipeline_free(lighting_pipe);
}

void LightingFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Albedo")) return;
    g.add(&lighting_pass);
}

}