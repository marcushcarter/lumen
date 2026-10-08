#include <core/rendering/features/editor/pick_feature.h>
#include <core/io/embedded_resource.h>
#include <algorithm>

namespace lumen {

void PickFeature::request(float p_u, float p_v)
{
    pending = true;
    pending_u = p_u;
    pending_v = p_v;
    if (++request_serial == 0) request_serial = 1;
}

bool PickFeature::take_result(uint32_t& r_entity_index)
{
    if (!result_ready) return false;
    result_ready = false;
    r_entity_index = result;
    return true;
}

void PickFeature::_create_pick_pass()
{
    pick_pass.name = "EditorPick";
    pick_pass.category = PASS_CATEGORY_EDITOR;
    pick_pass.never_cull = true;
    pick_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("PickResult", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    pick_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* vis = cl.graph->image("G_Visibility");
        auto* depth = cl.graph->image("G_Depth");
        auto* refs = cl.graph->buffer("ClusterRefs");
        auto* instances = cl.graph->buffer("Instances");
        auto* out = cl.graph->buffer("PickResult");
        if (!vis || !depth || !refs || !instances || !out) return;
        if (vis->extent.width == 0 || vis->extent.height == 0) return;

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress result_addr;
            uint32_t vis_index;
            uint32_t depth_index;
            uint32_t x;
            uint32_t y;
        } pc{};
        pc.cluster_refs_addr = refs->device_address;
        pc.instances_addr = instances->device_address;
        pc.result_addr = out->device_address;
        pc.vis_index = vis->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.x = std::min((uint32_t)(std::clamp(pass_u, 0.0f, 1.0f) * (float)vis->extent.width), vis->extent.width - 1);
        pc.y = std::min((uint32_t)(std::clamp(pass_v, 0.0f, 1.0f) * (float)vis->extent.height), vis->extent.height - 1);

        cl.dd->command_bind_pipeline(cl.cmd, pick_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Editor pick", 1);
    };
}

Error PickFeature::create_resources()
{
    using enum Error;

    const uint32_t count = ctx->graph->frame_count;
    readback.resize(count);
    slot_serial.assign(count, 0);
    for (uint32_t i = 0; i < count; i++) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = sizeof(uint32_t);
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.cpu_read = true;
        ci.name = "PickResult";
        readback[i] = ctx->dd->buffer_create(ci);
        LUMEN_ERR_FAIL_COND_V(!readback[i].buffer, FAILED);
    }
    _create_pick_pass();
    return OK;
}

Error PickFeature::create_pipelines()
{
    using enum Error;

    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_EDITOR_PICK_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "editor/pick.comp" });
    pick_pipe = ctx->dd->compute_pipeline_create({ cs, "editor/pick" });
    ctx->dd->shader_free(cs);
    return OK;
}

void PickFeature::destroy_resources()
{
    ctx->dd->pipeline_free(pick_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : readback) ctx->dd->buffer_free(b);
    readback.clear();
    slot_serial.clear();
}

void PickFeature::build(RenderGraph& g)
{
    if (readback.empty()) return;
    const uint32_t slot = g.current_frame;

    if (slot_serial[slot] != 0) {
        ctx->dd->buffer_invalidate(readback[slot]);
        const uint32_t entity = *static_cast<const uint32_t*>(readback[slot].mapped);
        if (slot_serial[slot] == request_serial) {
            result = entity;
            result_ready = true;
        }
        slot_serial[slot] = 0;
    }

    if (!pending) return;
    if (!g.image_resource("G_Visibility") || !g.buffer_resource("ClusterRefs")) return;
    pending = false;
    pass_u = pending_u;
    pass_v = pending_v;
    slot_serial[slot] = request_serial;

    *static_cast<uint32_t*>(readback[slot].mapped) = NONE;
    ctx->dd->buffer_flush(readback[slot]);

    g.import_buffer("PickResult", &readback[slot], VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    g.add(&pick_pass);
}

}