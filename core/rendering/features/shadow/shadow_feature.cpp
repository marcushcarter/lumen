#include <core/rendering/features/shadow/shadow_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>

namespace lumen {

uint32_t ShadowFeature::_list_capacity() const
{
    return ctx->frame->instance_count + ctx->frame->cluster_ref_capacity / CHUNK + 1;
}

uint32_t ShadowFeature::_list_stride() const
{
    return LIST_HEADER + _list_capacity() * 2;
}

void ShadowFeature::_create_clear_pass()
{
    clear_pass.name = "ShadowCullClear";
    clear_pass.category = PASS_CATEGORY_SHADOWS;
    clear_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo lists_ci{};
        lists_ci.size = (VkDeviceSize)SHADOW_CASCADES * _list_stride() * sizeof(uint32_t);
        lists_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        lists_ci.device_local = true;
        b.create_buffer("ShadowWorkLists", lists_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo refs_ci{};
        refs_ci.size = (VkDeviceSize)SHADOW_CASCADES * CLUSTER_CAPACITY * 4 * sizeof(uint32_t);
        refs_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        refs_ci.device_local = true;
        b.create_buffer("ShadowClusterRefs", refs_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo indices_ci{};
        indices_ci.size = (VkDeviceSize)SHADOW_CASCADES * TRI_CAPACITY * 3 * sizeof(uint32_t);
        indices_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        indices_ci.device_local = true;
        b.create_buffer("ShadowIndices", indices_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo args_ci{};
        args_ci.size = (VkDeviceSize)SHADOW_CASCADES * ARGS_SIZE;
        args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        args_ci.device_local = true;
        b.create_buffer("ShadowDrawArgs", args_ci);

        b.write_buffer("ShadowWorkLists", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.write_buffer("ShadowDrawArgs", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    clear_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* lists = cl.graph->buffer("ShadowWorkLists");
        auto* args = cl.graph->buffer("ShadowDrawArgs");
        if (!lists || !args) return;
        const uint32_t header[LIST_HEADER] = { 0, 0, 1, 0 };
        uint32_t draw[SHADOW_CASCADES * ARGS_STRIDE] = {};
        for (uint32_t c = 0; c < SHADOW_CASCADES; c++) {
            draw[c * ARGS_STRIDE + 1] = 1;
            draw[c * ARGS_STRIDE + 2] = c * TRI_CAPACITY * 3;
        }
        const VkDeviceSize stride = (VkDeviceSize)_list_stride() * sizeof(uint32_t);
        for (uint32_t c = 0; c < SHADOW_CASCADES; c++) cl.update_buffer("Shadow list clear", *lists, header, sizeof(header), c * stride);
        cl.update_buffer("Shadow args clear", *args, draw, sizeof(draw));
    };
}

void ShadowFeature::_create_instance_cull_pass()
{
    instance_cull_pass.name = "ShadowInstanceCull";
    instance_cull_pass.category = PASS_CATEGORY_SHADOWS;
    instance_cull_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.write_buffer("ShadowWorkLists", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    instance_cull_pass.execute = [this](RenderGraph::CommandList& cl) {
        const uint32_t count = ctx->frame->instance_count;
        if (count == 0) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress direct_light_addr;
            VkDeviceAddress lists_addr;
            uint32_t instance_count;
            uint32_t list_stride;
            uint32_t list_capacity;
            uint32_t cascade_mask;
        } pc{};
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.direct_light_addr = cl.graph->buffer("DirectLight")->device_address;
        pc.lists_addr = cl.graph->buffer("ShadowWorkLists")->device_address;
        pc.instance_count = count;
        pc.list_stride = _list_stride();
        pc.list_capacity = _list_capacity();
        pc.cascade_mask = cascade_mask;

        cl.dd->command_bind_pipeline(cl.cmd, instance_cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Shadow instance cull", (count + 63) / 64, SHADOW_CASCADES);
    };
}

void ShadowFeature::_create_args_pass(RenderGraph::Pass& r_pass, uint32_t p_phase)
{
    r_pass.name = p_phase == 0 ? "ShadowListArgs" : "ShadowDrawClamp";
    r_pass.category = PASS_CATEGORY_SHADOWS;
    r_pass.setup = [p_phase](RenderGraph::Builder& b) {
        if (p_phase == 0) b.write_buffer("ShadowWorkLists", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        else b.write_buffer("ShadowDrawArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_pass.execute = [this, p_phase](RenderGraph::CommandList& cl) {
        if (ctx->frame->instance_count == 0) return;
        struct Push {
            VkDeviceAddress lists_addr;
            VkDeviceAddress args_addr;
            uint32_t cascades;
            uint32_t list_stride;
            uint32_t list_capacity;
            uint32_t cluster_capacity;
            uint32_t tri_capacity;
            uint32_t phase;
        } pc{};
        auto* lists = cl.graph->buffer("ShadowWorkLists");
        auto* args = cl.graph->buffer("ShadowDrawArgs");
        pc.lists_addr = lists ? lists->device_address : 0;
        pc.args_addr = args ? args->device_address : 0;
        pc.cascades = SHADOW_CASCADES;
        pc.list_stride = _list_stride();
        pc.list_capacity = _list_capacity();
        pc.cluster_capacity = CLUSTER_CAPACITY;
        pc.tri_capacity = TRI_CAPACITY;
        pc.phase = p_phase;

        cl.dd->command_bind_pipeline(cl.cmd, args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch(p_phase == 0 ? "Shadow list args" : "Shadow draw args", 1);
    };
}

void ShadowFeature::_create_cluster_cull_pass()
{
    cluster_cull_pass.name = "ShadowClusterCull";
    cluster_cull_pass.category = PASS_CATEGORY_SHADOWS;
    cluster_cull_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ShadowWorkLists", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ShadowClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ShadowDrawArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cluster_cull_pass.execute = [this](RenderGraph::CommandList& cl) {
        if (ctx->frame->instance_count == 0) return;
        auto* lists = cl.graph->buffer("ShadowWorkLists");

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress direct_light_addr;
            VkDeviceAddress lists_addr;
            VkDeviceAddress clusters_addr;
            VkDeviceAddress args_addr;
            uint32_t cascade;
            uint32_t list_stride;
            uint32_t cluster_capacity;
            float lod_texels;
            float lod_min_error;
        } pc{};
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.direct_light_addr = cl.graph->buffer("DirectLight")->device_address;
        pc.lists_addr = lists->device_address;
        pc.clusters_addr = cl.graph->buffer("ShadowClusterRefs")->device_address;
        pc.args_addr = cl.graph->buffer("ShadowDrawArgs")->device_address;
        pc.list_stride = _list_stride();
        pc.cluster_capacity = CLUSTER_CAPACITY;
        pc.lod_texels = LOD_TEXELS;
        pc.lod_min_error = LOD_MIN_ERROR;

        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        cl.dd->command_bind_pipeline(cl.cmd, cluster_cull_pipe);
        for (uint32_t c = 0; c < SHADOW_CASCADES; c++) {
            if (((cascade_mask >> c) & 1u) == 0) continue;
            pc.cascade = c;
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.dispatch_indirect("Shadow cluster cull", *lists, (VkDeviceSize)c * pc.list_stride * sizeof(uint32_t));
        }
    };
}

void ShadowFeature::_create_index_build_pass()
{
    index_build_pass.name = "ShadowIndexBuild";
    index_build_pass.category = PASS_CATEGORY_SHADOWS;
    index_build_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ShadowClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ShadowDrawArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ShadowIndices", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    index_build_pass.execute = [this](RenderGraph::CommandList& cl) {
        if (ctx->frame->instance_count == 0) return;
        auto* args = cl.graph->buffer("ShadowDrawArgs");

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress clusters_addr;
            VkDeviceAddress args_addr;
            VkDeviceAddress indices_addr;
            uint32_t cascade;
            uint32_t cluster_capacity;
            uint32_t tri_capacity;
        } pc{};
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.clusters_addr = cl.graph->buffer("ShadowClusterRefs")->device_address;
        pc.args_addr = args->device_address;
        pc.indices_addr = cl.graph->buffer("ShadowIndices")->device_address;
        pc.cluster_capacity = CLUSTER_CAPACITY;
        pc.tri_capacity = TRI_CAPACITY;

        cl.dd->command_bind_pipeline(cl.cmd, index_build_pipe);
        for (uint32_t c = 0; c < SHADOW_CASCADES; c++) {
            if (((cascade_mask >> c) & 1u) == 0) continue;
            pc.cascade = c;
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.dispatch_indirect("Shadow index build", *args, (VkDeviceSize)c * ARGS_SIZE + ARGS_BUILD_OFFSET);
        }
    };
}

void ShadowFeature::_create_cascade_pass(uint32_t p_index)
{
    RenderGraph::Pass& pass = cascade_passes[p_index];
    pass.name = CASCADE_NAMES[p_index];
    pass.category = PASS_CATEGORY_SHADOWS;
    pass.setup = [p_index](RenderGraph::Builder& b) {
        b.depth_attachment(CASCADE_NAMES[p_index], VK_ATTACHMENT_LOAD_OP_CLEAR);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ShadowClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ShadowIndices", VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
        b.read_buffer("ShadowDrawArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    };
    pass.execute = [this, p_index](RenderGraph::CommandList& cl) {
        if (ctx->frame->instance_count == 0 || !depth_pipe.pipeline) return;
        auto* map = cl.graph->image(CASCADE_NAMES[p_index]);
        auto* args = cl.graph->buffer("ShadowDrawArgs");
        auto* indices = cl.graph->buffer("ShadowIndices");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress direct_light_addr;
            VkDeviceAddress clusters_addr;
            uint32_t cascade;
            uint32_t cluster_capacity;
        } pc{};
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.direct_light_addr = cl.graph->buffer("DirectLight")->device_address;
        pc.clusters_addr = cl.graph->buffer("ShadowClusterRefs")->device_address;
        pc.cascade = p_index;
        pc.cluster_capacity = CLUSTER_CAPACITY;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, map->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, map->extent }});
        cl.dd->command_bind_pipeline(cl.cmd, depth_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.draw_indexed_indirect(CASCADE_NAMES[p_index], *args, (VkDeviceSize)p_index * ARGS_SIZE, 1, DRAW_CMD_SIZE);
    };
}

Error ShadowFeature::create_resources()
{
    _create_clear_pass();
    _create_instance_cull_pass();
    _create_args_pass(list_args_pass, 0);
    _create_cluster_cull_pass();
    _create_args_pass(draw_args_pass, 1);
    _create_index_build_pass();
    for (uint32_t i = 0; i < SHADOW_CASCADES; i++) {
        ctx->graph->declare_image_format(CASCADE_NAMES[i], VK_FORMAT_D16_UNORM);
        _create_cascade_pass(i);
    }
    return Error::OK;
}

Error ShadowFeature::create_pipelines()
{
    instance_cull_pipe = _compute_pipeline(L"SHADERS_CULLING_SHADOW_INSTANCE_CULL_COMP", "culling/shadow_instance_cull.comp");
    args_pipe = _compute_pipeline(L"SHADERS_CULLING_SHADOW_ARGS_FINALIZE_COMP", "culling/shadow_args_finalize.comp");
    cluster_cull_pipe = _compute_pipeline(L"SHADERS_CULLING_SHADOW_CLUSTER_CULL_COMP", "culling/shadow_cluster_cull.comp");
    index_build_pipe = _compute_pipeline(L"SHADERS_CULLING_SHADOW_INDEX_BUILD_COMP", "culling/shadow_index_build.comp");

    EmbeddedResource::Blob blob = EmbeddedResource::load(L"SHADERS_RASTER_SHADOW_DEPTH_VERT");
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.render_pass = ctx->graph->acquire_render_pass(cascade_passes[0]);
    ci.vertex_shader = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = "raster/shadow_depth.vert" });
    ci.cull_mode = VK_CULL_MODE_NONE;
    ci.depth_test = true;
    ci.depth_write = true;
    ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    ci.depth_clamp = true;
    ci.depth_bias_constant = -DEPTH_BIAS_CONSTANT;
    ci.depth_bias_slope = -DEPTH_BIAS_SLOPE;
    ci.name = "raster/shadow_depth";
    depth_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->dd->shader_free(ci.vertex_shader);
    return Error::OK;
}

void ShadowFeature::destroy_resources()
{
    ctx->dd->pipeline_free(instance_cull_pipe);
    ctx->dd->pipeline_free(args_pipe);
    ctx->dd->pipeline_free(cluster_cull_pipe);
    ctx->dd->pipeline_free(index_build_pipe);
    ctx->dd->pipeline_free(depth_pipe);
    instance_cull_pipe = {};
    args_pipe = {};
    cluster_cull_pipe = {};
    index_build_pipe = {};
    depth_pipe = {};
}

void ShadowFeature::build(RenderGraph& g)
{
    cascade_mask = 0;
    for (uint32_t c = 0; c < SHADOW_CASCADES; c++) {
        if (ctx->frame->shadow_render[c] && g.image_resource(CASCADE_NAMES[c])) cascade_mask |= 1u << c;
    }
    if (cascade_mask == 0) return;

    g.add(&clear_pass);
    g.add(&instance_cull_pass);
    g.add(&list_args_pass);
    g.add(&cluster_cull_pass);
    g.add(&draw_args_pass);
    g.add(&index_build_pass);
    for (uint32_t c = 0; c < SHADOW_CASCADES; c++) {
        if ((cascade_mask >> c) & 1u) g.add(&cascade_passes[c]);
    }
}

}
