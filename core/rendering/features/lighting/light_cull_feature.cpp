#include <core/rendering/features/lighting/light_cull_feature.h>
#include <core/io/embedded_resource.h>
#include <algorithm>

namespace lumen {
void LightCullFeature::_create_clear_pass()
{
    clear_pass.name = "LightCullClear";
    clear_pass.category = PASS_CATEGORY_LIGHTING;
    clear_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo data_ci{};
        data_ci.size = LIGHT_CULL_DATA_SIZE;
        data_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        data_ci.device_local = true;
        b.create_buffer("LightCullData", data_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo visible_ci{};
        visible_ci.size = (VkDeviceSize)MAX_VISIBLE_LIGHTS * 2 * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        visible_ci.device_local = true;
        b.create_buffer("VisibleLights", visible_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo sorted_ci{};
        sorted_ci.size = (VkDeviceSize)MAX_VISIBLE_LIGHTS * sizeof(GpuLight);
        sorted_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        sorted_ci.device_local = true;
        b.create_buffer("SortedLights", sorted_ci);

        const uint32_t tiles = std::max(1u, ((b.graph->width + LIGHT_TILE_SIZE - 1) / LIGHT_TILE_SIZE) * ((b.graph->height + LIGHT_TILE_SIZE - 1) / LIGHT_TILE_SIZE));
        drivers::DeviceDriverVulkan::BufferCreateInfo masks_ci{};
        masks_ci.size = (VkDeviceSize)tiles * LIGHT_TILE_WORDS * sizeof(uint32_t);
        masks_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        masks_ci.device_local = true;
        b.create_buffer("LightTileMasks", masks_ci);

        b.write_buffer("LightCullData", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    clear_pass.execute = [](RenderGraph::CommandList& cl) {
        auto* data = cl.graph->buffer("LightCullData");
        if (!data) return;
        cl.fill_buffer("Clear light counters", *data, 0u, 0, LIGHT_CULL_ZBINS_OFFSET);
        cl.fill_buffer("Clear light zbins", *data, 0xFFFFFFFFu, LIGHT_CULL_ZBINS_OFFSET, (VkDeviceSize)LIGHT_ZBIN_COUNT * 2 * sizeof(uint32_t));
    };
}

void LightCullFeature::_create_cull_pass()
{
    cull_pass.name = "LightCull";
    cull_pass.category = PASS_CATEGORY_LIGHTING;
    cull_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Lights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("LightCullData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cull_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* camera = cl.graph->buffer("Camera");
        auto* lights = cl.graph->buffer("Lights");
        auto* data = cl.graph->buffer("LightCullData");
        auto* visible = cl.graph->buffer("VisibleLights");
        const uint32_t local = _local_count();
        if (!camera || !lights || !data || !visible || local == 0) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress lights_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress visible_addr;
            uint32_t first_light;
            uint32_t local_count;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.lights_addr = lights->device_address;
        pc.cull_addr = data->device_address;
        pc.visible_addr = visible->device_address;
        pc.first_light = MAX_DIRECTIONAL_LIGHTS;
        pc.local_count = local;

        cl.dd->command_bind_pipeline(cl.cmd, cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Light cull", (local + 63) / 64);
    };
}

void LightCullFeature::_create_bin_pass()
{
    bin_pass.name = "LightBin";
    bin_pass.category = PASS_CATEGORY_LIGHTING;
    bin_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Lights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("LightCullData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("SortedLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    bin_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* lights = cl.graph->buffer("Lights");
        auto* data = cl.graph->buffer("LightCullData");
        auto* visible = cl.graph->buffer("VisibleLights");
        auto* sorted = cl.graph->buffer("SortedLights");
        if (!lights || !data || !visible || !sorted || _local_count() == 0) return;

        struct Push {
            VkDeviceAddress lights_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress sorted_addr;
        } pc{};
        pc.lights_addr = lights->device_address;
        pc.cull_addr = data->device_address;
        pc.visible_addr = visible->device_address;
        pc.sorted_addr = sorted->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, bin_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Light bin", MAX_VISIBLE_LIGHTS / 256);
    };
}

void LightCullFeature::_create_tiles_pass()
{
    tiles_pass.name = "LightTiles";
    tiles_pass.category = PASS_CATEGORY_LIGHTING;
    tiles_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("LightCullData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SortedLights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("LightTileMasks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    tiles_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* camera = cl.graph->buffer("Camera");
        auto* data = cl.graph->buffer("LightCullData");
        auto* sorted = cl.graph->buffer("SortedLights");
        auto* masks = cl.graph->buffer("LightTileMasks");
        if (!depth || !camera || !data || !sorted || !masks || _local_count() == 0) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress sorted_addr;
            VkDeviceAddress masks_addr;
            uint32_t depth_index;
            uint32_t width;
            uint32_t height;
        } pc{};
        pc.camera_addr = camera->device_address;
        pc.cull_addr = data->device_address;
        pc.sorted_addr = sorted->device_address;
        pc.masks_addr = masks->device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.width = depth->extent.width;
        pc.height = depth->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, tiles_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Light tiles", (pc.width + LIGHT_TILE_SIZE - 1) / LIGHT_TILE_SIZE, (pc.height + LIGHT_TILE_SIZE - 1) / LIGHT_TILE_SIZE);
    };
}

Error LightCullFeature::create_resources()
{
    _create_clear_pass();
    _create_cull_pass();
    _create_bin_pass();
    _create_tiles_pass();
    return Error::OK;
}

drivers::DeviceDriverVulkan::Pipeline LightCullFeature::_compute_pipeline(const wchar_t* p_resource, const char* p_name)
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    drivers::DeviceDriverVulkan::Pipeline pipe = ctx->dd->compute_pipeline_create({ cs, p_name });
    ctx->dd->shader_free(cs);
    return pipe;
}

Error LightCullFeature::create_pipelines()
{
    cull_pipe = _compute_pipeline(L"SHADERS_LIGHTING_LIGHT_CULL_COMP", "lighting/light_cull.comp");
    bin_pipe = _compute_pipeline(L"SHADERS_LIGHTING_LIGHT_BIN_COMP", "lighting/light_bin.comp");
    tiles_pipe = _compute_pipeline(L"SHADERS_LIGHTING_LIGHT_TILES_COMP", "lighting/light_tiles.comp");
    return Error::OK;
}

void LightCullFeature::destroy_resources()
{
    ctx->dd->pipeline_free(cull_pipe);
    ctx->dd->pipeline_free(bin_pipe);
    ctx->dd->pipeline_free(tiles_pipe);
}

void LightCullFeature::build(RenderGraph& g)
{
    if (!g.image_resource("G_Depth")) return;
    g.add(&clear_pass);
    g.add(&cull_pass);
    g.add(&bin_pass);
    g.add(&tiles_pass);
}

}