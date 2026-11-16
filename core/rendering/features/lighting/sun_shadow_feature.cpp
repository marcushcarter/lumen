#include <core/rendering/features/lighting/sun_shadow_feature.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>
#include <algorithm>

namespace lumen {

void SunShadowFeature::_create_reset_pass()
{
    reset_pass.name = "PlayerShadowReset";
    reset_pass.category = PASS_CATEGORY_SHADOWS;
    reset_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("PlayerShadowList", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    reset_pass.execute = [](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer("PlayerShadowList");
        if (!list) return;
        const uint32_t header[8] = { 0u, 1u, 0u, 0u, 0u, 0u, 0u, UINT32_MAX };
        cl.update_buffer("Player shadow reset", *list, header, sizeof(header));
    };
}

void SunShadowFeature::_create_select_pass()
{
    select_pass.name = "PlayerShadowSelect";
    select_pass.category = PASS_CATEGORY_SHADOWS;
    select_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.write_buffer("PlayerShadowList", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("PlayerShadowIndices", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    select_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* geometry = cl.graph->buffer("Geometry");
        auto* list = cl.graph->buffer("PlayerShadowList");
        auto* indices = cl.graph->buffer("PlayerShadowIndices");
        if (!geometry || !list || !indices) return;

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress indices_addr;
            uint32_t mesh;
            uint32_t cluster_capacity;
            uint32_t index_capacity;
            float lod_density;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.geometry_addr = geometry->device_address;
        pc.list_addr = list->device_address;
        pc.indices_addr = indices->device_address;
        pc.mesh = ctx->frame->player_mesh;
        pc.cluster_capacity = PLAYER_SHADOW_MAX_CLUSTERS;
        pc.index_capacity = PLAYER_SHADOW_MAX_INDICES;
        pc.lod_density = ctx->frame->player_lod_density;

        cl.dd->command_bind_pipeline(cl.cmd, select_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Player shadow select", std::max(1u, (ctx->frame->player_node_count + GROUP_SIZE - 1) / GROUP_SIZE));
    };
}

void SunShadowFeature::_create_finalize_pass()
{
    finalize_pass.name = "PlayerShadowFinalize";
    finalize_pass.category = PASS_CATEGORY_SHADOWS;
    finalize_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("PlayerShadowList", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    finalize_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer("PlayerShadowList");
        if (!list) return;
        VkDeviceAddress addr = list->device_address;
        cl.dd->command_bind_pipeline(cl.cmd, finalize_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(addr), &addr);
        cl.dispatch("Player shadow finalize", 1);
    };
}

void SunShadowFeature::_create_draw_pass()
{
    draw_pass.name = "PlayerShadow";
    draw_pass.category = PASS_CATEGORY_SHADOWS;
    draw_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "PlayerShadow";
        ci.format = VK_FORMAT_D16_UNORM;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
        ci.fixed_width = PLAYER_SHADOW_SIZE;
        ci.fixed_height = PLAYER_SHADOW_SIZE;
        b.create_image("PlayerShadow", ci);
        b.depth_attachment("PlayerShadow", VK_ATTACHMENT_LOAD_OP_CLEAR, [] { VkClearValue v{}; v.depthStencil = { 1.0f, 0 }; return v; }());
        b.read_buffer("PlayerShadowList", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("PlayerShadowIndices", VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    draw_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* shadow = cl.graph->image("PlayerShadow");
        auto* list = cl.graph->buffer("PlayerShadowList");
        auto* indices = cl.graph->buffer("PlayerShadowIndices");
        auto* camera = cl.graph->buffer("Camera");
        auto* light = cl.graph->buffer("DirectLight");
        auto* geometry = cl.graph->buffer("Geometry");
        auto* instances = cl.graph->buffer("Instances");
        auto* transforms = cl.graph->buffer("Transforms");
        if (!shadow || !list || !indices || !camera || !light || !geometry || !instances || !transforms) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress direct_light_addr;
            uint32_t instance;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = instances->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.list_addr = list->device_address;
        pc.direct_light_addr = light->device_address;
        pc.instance = ctx->frame->player_instance;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, shadow->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, shadow->extent }});
        cl.dd->command_bind_pipeline(cl.cmd, draw_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.draw_indexed_indirect("Player shadow", *list, 0, 1, DRAW_STRIDE);
    };
}

bool SunShadowFeature::_slot_stale(const Slot& p_slot) const
{
    const FrameData& f = *ctx->frame;
    return p_slot.mesh != f.player_mesh || p_slot.lod_density != f.player_lod_density || p_slot.geometry_version != ctx->geometry->version || p_slot.residency_version != ctx->geometry->residency_version;
}

void SunShadowFeature::_slot_mark(Slot& r_slot) const
{
    const FrameData& f = *ctx->frame;
    r_slot.mesh = f.player_mesh;
    r_slot.lod_density = f.player_lod_density;
    r_slot.geometry_version = ctx->geometry->version;
    r_slot.residency_version = ctx->geometry->residency_version;
}

Error SunShadowFeature::create_resources()
{
    slots.resize(ctx->graph->frame_count);
    for (Slot& s : slots) {
        s = {};
        s.list = ctx->dd->buffer_create({ .size = LIST_HEADER_SIZE + (VkDeviceSize)PLAYER_SHADOW_MAX_CLUSTERS * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = true, .name = "player_shadow_list" });
        s.indices = ctx->dd->buffer_create({ .size = (VkDeviceSize)PLAYER_SHADOW_MAX_INDICES * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .device_local = true, .name = "player_shadow_indices" });
        LUMEN_ERR_FAIL_COND_V(!s.list.buffer || !s.indices.buffer, Error::FAILED);
    }
    _create_reset_pass();
    _create_select_pass();
    _create_finalize_pass();
    _create_draw_pass();
    return Error::OK;
}

Error SunShadowFeature::create_pipelines()
{
    select_pipe = _compute_pipeline(L"SHADERS_LIGHTING_PLAYER_SHADOW_SELECT_COMP", "lighting/player_shadow_select.comp");
    finalize_pipe = _compute_pipeline(L"SHADERS_LIGHTING_PLAYER_SHADOW_FINALIZE_COMP", "lighting/player_shadow_finalize.comp");

    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_LIGHTING_PLAYER_SHADOW_VERT");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "lighting/player_shadow.vert" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.render_pass = ctx->graph->acquire_render_pass(draw_pass);
    ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.cull_mode = VK_CULL_MODE_NONE;
    ci.depth_test = true;
    ci.depth_write = true;
    ci.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
    ci.name = "lighting/player_shadow";
    draw_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->dd->shader_free(vs);
    for (Slot& s : slots) s.mesh = UINT32_MAX;
    return Error::OK;
}

void SunShadowFeature::destroy_resources()
{
    ctx->dd->pipeline_free(select_pipe);
    ctx->dd->pipeline_free(finalize_pipe);
    ctx->dd->pipeline_free(draw_pipe);
    select_pipe = {};
    finalize_pipe = {};
    draw_pipe = {};
    for (Slot& s : slots) {
        ctx->dd->buffer_free(s.list);
        ctx->dd->buffer_free(s.indices);
    }
    slots.clear();
}

void SunShadowFeature::build(RenderGraph& g)
{
    if (!ctx->frame || !ctx->geometry || slots.empty()) return;
    const FrameData& f = *ctx->frame;
    if (f.player_instance == UINT32_MAX || f.player_mesh == UINT32_MAX || f.direct_light.player_shadow_params.z == 0.0f) return;
    if (!g.buffer_resource("Instances") || !g.buffer_resource("Geometry")) return;

    Slot& slot = slots[g.current_frame % slots.size()];
    g.import_buffer("PlayerShadowList", &slot.list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    g.import_buffer("PlayerShadowIndices", &slot.indices, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    if (_slot_stale(slot)) {
        g.add(&reset_pass);
        g.add(&select_pass);
        g.add(&finalize_pass);
        _slot_mark(slot);
    }
    g.add(&draw_pass);
}

}
