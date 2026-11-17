#include <core/rendering/features/lighting/sun_shadow_feature.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/rendering/resources/terrain_cache.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cmath>
#include <cstring>

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
        const uint32_t header[LIST_HEADER_WORDS] = {};
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
        b.write_buffer("PlayerShadowMaterials", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    select_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* geometry = cl.graph->buffer("Geometry");
        auto* list = cl.graph->buffer("PlayerShadowList");
        auto* indices = cl.graph->buffer("PlayerShadowIndices");
        auto* materials = cl.graph->buffer("PlayerShadowMaterials");
        if (!geometry || !list || !indices || !materials) return;

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress indices_addr;
            VkDeviceAddress materials_addr;
            uint32_t mesh;
            uint32_t cluster_capacity;
            uint32_t index_capacity;
            float lod_density;
            uint32_t slot_base;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.geometry_addr = geometry->device_address;
        pc.list_addr = list->device_address;
        pc.indices_addr = indices->device_address;
        pc.materials_addr = materials->device_address;
        pc.mesh = ctx->frame->player_mesh;
        pc.cluster_capacity = PLAYER_SHADOW_MAX_CLUSTERS;
        pc.index_capacity = PLAYER_SHADOW_MAX_INDICES;
        pc.lod_density = ctx->frame->player_lod_density;
        pc.slot_base = ctx->frame->player_slot_base;

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
        struct Push {
            VkDeviceAddress list_addr;
            uint32_t triangle_capacity;
            uint32_t cluster_capacity;
        } pc{ list->device_address, PLAYER_SHADOW_MAX_TRIANGLES, PLAYER_SHADOW_MAX_CLUSTERS };
        cl.dd->command_bind_pipeline(cl.cmd, finalize_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Player shadow finalize", 1);
    };
}

void SunShadowFeature::_create_skin_pass()
{
    skin_pass.name = "PlayerShadowSkin";
    skin_pass.category = PASS_CATEGORY_SHADOWS;
    skin_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("PlayerShadowList", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("SkinCache", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("PlayerSkinTable", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    skin_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer("PlayerShadowList");
        auto* table = cl.graph->buffer("PlayerSkinTable");
        if (!list || !table || !ctx->frame->skin_cache_header) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress skin_cache_addr;
            VkDeviceAddress player_table_addr;
            VkDeviceAddress shadow_list_addr;
            uint32_t write_prev;
            uint32_t mode;
            uint32_t player_instance;
            uint32_t shadow_capacity;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.skin_cache_addr = ctx->frame->skin_cache_header;
        pc.player_table_addr = table->device_address;
        pc.shadow_list_addr = list->device_address;
        pc.mode = 1u;
        pc.player_instance = ctx->frame->player_instance;
        pc.shadow_capacity = PLAYER_SHADOW_MAX_CLUSTERS;

        cl.dd->command_bind_pipeline(cl.cmd, skin_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Player shadow skin", *list, LIST_SKIN_DISPATCH_OFFSET);
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
        b.read_buffer("PlayerShadowMaterials", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (b.graph->buffer_resource("PlayerSkinTable") && b.graph->buffer_resource("SkinCache")) {
            b.read_buffer("PlayerSkinTable", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.read_buffer("SkinCache", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        }
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
        auto* materials = cl.graph->buffer("PlayerShadowMaterials");
        if (!shadow || !list || !indices || !camera || !light || !geometry || !instances || !transforms || !materials) return;

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress direct_light_addr;
            VkDeviceAddress materials_addr;
            VkDeviceAddress player_table_addr;
            VkDeviceAddress skin_cache_addr;
            uint32_t instance;
            uint32_t sampler_index;
        } pc{};
        const auto* table = cl.graph->buffer("PlayerSkinTable");
        const bool cached = table && cl.graph->buffer("SkinCache") && ctx->frame->skin_cache_header;
        pc.player_table_addr = cached ? table->device_address : 0;
        pc.skin_cache_addr = cached ? ctx->frame->skin_cache_header : 0;
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = instances->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.list_addr = list->device_address;
        pc.direct_light_addr = light->device_address;
        pc.materials_addr = materials->device_address;
        pc.instance = ctx->frame->player_instance;
        pc.sampler_index = material_sampler.bindless_sampler;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, shadow->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, shadow->extent }});
        cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_pipeline(cl.cmd, draw_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect("Player shadow opaque", *list, 0, 1, DRAW_STRIDE);
        cl.dd->command_bind_pipeline(cl.cmd, masked_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect("Player shadow masked", *list, DRAW_STRIDE, 1, DRAW_STRIDE);
    };
}

void SunShadowFeature::_create_cascade_passes(uint32_t p_cascade)
{
    const uint32_t c = p_cascade;
    const std::string index = std::to_string(c);
    cascade_names[c][NAME_LIST] = "SunCascadeList" + index;
    cascade_names[c][NAME_WORK] = "SunCascadeWork" + index;
    cascade_names[c][NAME_IMAGE] = "SunCascade" + index;
    cascade_names[c][NAME_INDICES] = "SunCascadeIndices" + index;
    cascade_names[c][NAME_MATERIALS] = "SunCascadeMaterials" + index;
    cascade_names[c][NAME_SPILL] = "SunCascadeSpill" + index;
    cascade_names[c][NAME_FROZEN_LIST] = "SunCascadeFrozenList" + index;
    cascade_names[c][NAME_FROZEN_INDICES] = "SunCascadeFrozenIndices" + index;
    cascade_names[c][NAME_FROZEN_MATERIALS] = "SunCascadeFrozenMaterials" + index;
    cascade_names[c][NAME_FROZEN_SPILL] = "SunCascadeFrozenSpill" + index;
    cascade_names[c][NAME_FEEDBACK] = "SunCascadeFeedback" + index;

    RenderGraph::Pass& reset = cascade_reset_pass[c];
    reset.name = "SunCascadeReset" + index;
    reset.category = PASS_CATEGORY_SHADOWS;
    reset.setup = [this, c](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo list_ci{};
        list_ci.size = CASCADE_LIST_SIZE;
        list_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        list_ci.device_local = true;
        b.create_buffer(cascade_names[c][NAME_LIST], list_ci);
        drivers::DeviceDriverVulkan::BufferCreateInfo index_ci{};
        index_ci.size = _cascade_index_size(c);
        index_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        index_ci.device_local = true;
        b.create_buffer(cascade_names[c][NAME_INDICES], index_ci);
        drivers::DeviceDriverVulkan::BufferCreateInfo material_ci{};
        material_ci.size = _cascade_material_size(c);
        material_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        material_ci.device_local = true;
        b.create_buffer(cascade_names[c][NAME_MATERIALS], material_ci);
        drivers::DeviceDriverVulkan::BufferCreateInfo spill_ci{};
        spill_ci.size = CASCADE_SPILL_SIZE;
        spill_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        spill_ci.device_local = true;
        b.create_buffer(cascade_names[c][NAME_SPILL], spill_ci);
        b.write_buffer(cascade_names[c][NAME_LIST], VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    reset.execute = [this, c](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer(cascade_names[c][NAME_LIST]);
        if (!list) return;
        uint32_t header[CASCADE_HEADER_WORDS] = {};
        header[CASCADE_CAPACITY_WORD] = SUN_CASCADE_MAX_CLUSTERS;
        cl.update_buffer("Sun cascade reset", *list, header, sizeof(header));
    };

    RenderGraph::Pass& select = cascade_select_pass[c];
    select.name = "SunCascadeSelect" + index;
    select.category = PASS_CATEGORY_SHADOWS;
    select.setup = [this, c](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer(cascade_names[c][NAME_WORK], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (b.graph->buffer_resource("SunReceivers")) b.read_buffer("SunReceivers", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer(cascade_names[c][NAME_LIST], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    select.execute = [this, c](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer(cascade_names[c][NAME_LIST]);
        auto* work = cl.graph->buffer(cascade_names[c][NAME_WORK]);
        if (!list || !work || cascade_work_count[c] == 0) return;
        auto* receivers = cl.graph->buffer("SunReceivers");
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress light_addr;
            VkDeviceAddress work_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress receivers_addr;
            uint32_t cascade;
            uint32_t work_count;
            float density;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.light_addr = cl.graph->buffer("DirectLight")->device_address;
        pc.work_addr = work->device_address;
        pc.list_addr = list->device_address;
        pc.receivers_addr = receivers ? receivers->device_address : 0;
        pc.cascade = c;
        pc.work_count = cascade_work_count[c];
        pc.density = ctx->frame->px_per_unit / CASCADE_LOD_DISTANCE[c];

        const uint32_t groups_x = std::min(pc.work_count, CASCADE_DISPATCH_MAX);
        const uint32_t groups_y = (pc.work_count + CASCADE_DISPATCH_MAX - 1) / CASCADE_DISPATCH_MAX;
        cl.dd->command_bind_pipeline(cl.cmd, cascade_select_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sun cascade select", groups_x, groups_y);
    };

    auto args_pass = [this, c, &index](RenderGraph::Pass& r_pass, const char* p_name, uint32_t p_mode) {
        r_pass.name = std::string(p_name) + index;
        r_pass.category = PASS_CATEGORY_SHADOWS;
        r_pass.setup = [this, c](RenderGraph::Builder& b) {
            b.write_buffer(cascade_names[c][NAME_LIST], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        };
        r_pass.execute = [this, c, p_mode](RenderGraph::CommandList& cl) {
            auto* list = cl.graph->buffer(cascade_names[c][NAME_LIST]);
            if (!list) return;
            struct Push {
                VkDeviceAddress list_addr;
                uint32_t mode;
                uint32_t spill_vertices;
                uint32_t capacity[CASCADE_KINDS];
                uint32_t base[CASCADE_KINDS];
            } pc{};
            static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
            pc.list_addr = list->device_address;
            pc.mode = p_mode;
            pc.spill_vertices = std::max(ctx->geometry->max_cluster_tris, 1u) * 3u;
            for (uint32_t k = 0; k < CASCADE_KINDS; k++) pc.capacity[k] = cascade_capacity[c][k];
            _cascade_bases(c, pc.base);
            cl.dd->command_bind_pipeline(cl.cmd, cascade_args_pipe);
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.dispatch(p_mode == 0 ? "Sun cascade dispatch args" : "Sun cascade draw args", 1);
        };
    };
    args_pass(cascade_dispatch_args_pass[c], "SunCascadeDispatchArgs", 0);
    args_pass(cascade_draw_args_pass[c], "SunCascadeDrawArgs", 1);

    RenderGraph::Pass& compact = cascade_compact_pass[c];
    compact.name = "SunCascadeCompact" + index;
    compact.category = PASS_CATEGORY_SHADOWS;
    compact.setup = [this, c](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer(cascade_names[c][NAME_LIST], VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer(cascade_names[c][NAME_INDICES], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer(cascade_names[c][NAME_MATERIALS], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer(cascade_names[c][NAME_SPILL], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    compact.execute = [this, c](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer(cascade_names[c][NAME_LIST]);
        auto* indices = cl.graph->buffer(cascade_names[c][NAME_INDICES]);
        auto* materials = cl.graph->buffer(cascade_names[c][NAME_MATERIALS]);
        auto* spill = cl.graph->buffer(cascade_names[c][NAME_SPILL]);
        if (!list || !indices || !materials || !spill) return;
        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress indices_addr;
            VkDeviceAddress materials_addr;
            VkDeviceAddress spill_addr;
            uint32_t capacity[CASCADE_KINDS];
            uint32_t base[CASCADE_KINDS];
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.list_addr = list->device_address;
        pc.indices_addr = indices->device_address;
        pc.materials_addr = materials->device_address;
        pc.spill_addr = spill->device_address;
        for (uint32_t k = 0; k < CASCADE_KINDS; k++) pc.capacity[k] = cascade_capacity[c][k];
        _cascade_bases(c, pc.base);
        cl.dd->command_bind_pipeline(cl.cmd, cascade_compact_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Sun cascade compact", *list, CASCADE_COMPACT_DISPATCH_OFFSET);
    };

    RenderGraph::Pass& feedback = cascade_feedback_pass[c];
    feedback.name = "SunCascadeFeedback" + index;
    feedback.category = PASS_CATEGORY_SHADOWS;
    feedback.never_cull = true;
    feedback.setup = [this, c](RenderGraph::Builder& b) {
        b.read_buffer(cascade_names[c][NAME_LIST], VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.write_buffer(cascade_names[c][NAME_FEEDBACK], VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    feedback.execute = [this, c](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer(cascade_names[c][NAME_LIST]);
        auto* dst = cl.graph->buffer(cascade_names[c][NAME_FEEDBACK]);
        if (!list || !dst) return;
        cl.dd->command_copy_buffer(cl.cmd, *list, *dst, CASCADE_FEEDBACK_WORDS * sizeof(uint32_t), CASCADE_FEEDBACK_OFFSET, 0);
        VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cl.cmd, &dep);
    };

    RenderGraph::Pass& capture = cascade_capture_pass[c];
    capture.name = "SunCascadeCapture" + index;
    capture.category = PASS_CATEGORY_SHADOWS;
    capture.never_cull = true;
    capture.setup = [this, c](RenderGraph::Builder& b) {
        for (uint32_t i = 0; i < 4; i++) {
            b.read_buffer(cascade_names[c][NAME_LIST + (i == 0 ? 0 : i + 2)], VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            b.write_buffer(cascade_names[c][NAME_FROZEN_LIST + i], VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        }
    };
    capture.execute = [this, c](RenderGraph::CommandList& cl) {
        const VkDeviceSize sizes[4] = { CASCADE_LIST_SIZE, _cascade_index_size(c), _cascade_material_size(c), CASCADE_SPILL_SIZE };
        for (uint32_t i = 0; i < 4; i++) {
            auto* s = cl.graph->buffer(cascade_names[c][NAME_LIST + (i == 0 ? 0 : i + 2)]);
            auto* d = cl.graph->buffer(cascade_names[c][NAME_FROZEN_LIST + i]);
            if (s && d) cl.dd->command_copy_buffer(cl.cmd, *s, *d, sizes[i], 0, 0);
        }
    };

    RenderGraph::Pass& draw = cascade_draw_pass[c];
    draw.name = "SunCascade" + index;
    draw.category = PASS_CATEGORY_SHADOWS;
    draw.setup = [this, c](RenderGraph::Builder& b) {
        const bool replay = cascade_replay[c];
        b.depth_attachment(cascade_names[c][NAME_IMAGE], VK_ATTACHMENT_LOAD_OP_CLEAR, [] { VkClearValue v{}; v.depthStencil = { 1.0f, 0 }; return v; }());
        b.read_buffer(cascade_names[c][replay ? NAME_FROZEN_LIST : NAME_LIST], VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(cascade_names[c][replay ? NAME_FROZEN_INDICES : NAME_INDICES], VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
        b.read_buffer(cascade_names[c][replay ? NAME_FROZEN_MATERIALS : NAME_MATERIALS], VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(cascade_names[c][replay ? NAME_FROZEN_SPILL : NAME_SPILL], VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    draw.execute = [this, c](RenderGraph::CommandList& cl) {
        const bool replay = cascade_replay[c];
        auto* image = cl.graph->image(cascade_names[c][NAME_IMAGE]);
        auto* list = cl.graph->buffer(cascade_names[c][replay ? NAME_FROZEN_LIST : NAME_LIST]);
        auto* indices = cl.graph->buffer(cascade_names[c][replay ? NAME_FROZEN_INDICES : NAME_INDICES]);
        auto* materials = cl.graph->buffer(cascade_names[c][replay ? NAME_FROZEN_MATERIALS : NAME_MATERIALS]);
        auto* spill = cl.graph->buffer(cascade_names[c][replay ? NAME_FROZEN_SPILL : NAME_SPILL]);
        auto* camera = cl.graph->buffer("Camera");
        auto* light = cl.graph->buffer("DirectLight");
        auto* geometry = cl.graph->buffer("Geometry");
        auto* instances = cl.graph->buffer("Instances");
        auto* transforms = cl.graph->buffer("Transforms");
        if (!image || !list || !indices || !materials || !spill || !camera || !light || !geometry || !instances || !transforms) return;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, image->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, image->extent }});

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress light_addr;
            VkDeviceAddress materials_addr;
            VkDeviceAddress spill_addr;
            uint32_t cascade;
            uint32_t sampler_index;
            float mip_bias;
            uint32_t kind;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = instances->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.list_addr = list->device_address;
        pc.light_addr = light->device_address;
        pc.materials_addr = materials->device_address;
        pc.spill_addr = spill->device_address;
        pc.cascade = c;
        pc.sampler_index = material_sampler.bindless_sampler;
        pc.mip_bias = CASCADE_MASK_MIP_BIAS[c];

        cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
        const drivers::DeviceDriverVulkan::Pipeline* pipes[CASCADE_KINDS] = { &cascade_opaque_pipe, &cascade_two_sided_pipe, &cascade_masked_pipe, &cascade_mixed_pipe };
        static const char* NAMES[CASCADE_KINDS] = { "Sun cascade opaque", "Sun cascade two-sided", "Sun cascade masked", "Sun cascade masked mixed" };
        for (uint32_t k = 0; k < CASCADE_KINDS; k++) {
            pc.kind = k;
            cl.dd->command_bind_pipeline(cl.cmd, *pipes[k]);
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.draw_indexed_indirect(NAMES[k], *list, (uint64_t)k * CASCADE_DRAW_STRIDE, 1, CASCADE_DRAW_STRIDE);
        }

        const drivers::DeviceDriverVulkan::Pipeline* spill_pipes[CASCADE_KINDS] = { &cascade_spill_opaque_pipe, &cascade_spill_two_sided_pipe, &cascade_spill_masked_pipe, &cascade_spill_masked_pipe };
        static const char* SPILL_NAMES[CASCADE_KINDS] = { "Sun cascade spill opaque", "Sun cascade spill two-sided", "Sun cascade spill masked", "Sun cascade spill masked mixed" };
        for (uint32_t k = 0; k < CASCADE_KINDS; k++) {
            pc.kind = k;
            cl.dd->command_bind_pipeline(cl.cmd, *spill_pipes[k]);
            cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
            cl.draw_indirect(SPILL_NAMES[k], *list, CASCADE_SPILL_DRAW_OFFSET + (uint64_t)k * CASCADE_SPILL_DRAW_STRIDE, 1, CASCADE_SPILL_DRAW_STRIDE);
        }
    };
}

void SunShadowFeature::_create_receiver_passes()
{
    receiver_clear_pass.name = "SunReceiverClear";
    receiver_clear_pass.category = PASS_CATEGORY_SHADOWS;
    receiver_clear_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = (VkDeviceSize)SUN_CASCADE_COUNT * SUN_RECEIVER_CELLS * sizeof(uint32_t);
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ci.device_local = true;
        b.create_buffer("SunReceivers", ci);
        b.write_buffer("SunReceivers", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    receiver_clear_pass.execute = [](RenderGraph::CommandList& cl) {
        if (auto* receivers = cl.graph->buffer("SunReceivers")) cl.fill_buffer("Sun receiver clear", *receivers, 0);
    };

    receiver_mask_pass.name = "SunReceiverMask";
    receiver_mask_pass.category = PASS_CATEGORY_SHADOWS;
    receiver_mask_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.write_buffer("SunReceivers", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    receiver_mask_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* receivers = cl.graph->buffer("SunReceivers");
        if (!depth || !receivers) return;
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress light_addr;
            VkDeviceAddress receivers_addr;
            uint32_t depth_index;
            uint32_t width;
            uint32_t height;
        } pc{ cl.graph->buffer("Camera")->device_address, cl.graph->buffer("DirectLight")->device_address, receivers->device_address, depth->bindless_sampled, depth->extent.width, depth->extent.height };
        cl.dd->command_bind_pipeline(cl.cmd, receiver_mask_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sun receiver mask", (depth->extent.width + 31) / 32, (depth->extent.height + 31) / 32);
    };

    receiver_pyramid_pass.name = "SunReceiverPyramid";
    receiver_pyramid_pass.category = PASS_CATEGORY_SHADOWS;
    receiver_pyramid_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("SunReceivers", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    receiver_pyramid_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* receivers = cl.graph->buffer("SunReceivers");
        if (!receivers) return;
        VkDeviceAddress addr = receivers->device_address;
        cl.dd->command_bind_pipeline(cl.cmd, receiver_pyramid_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(addr), &addr);
        cl.dispatch("Sun receiver pyramid", SUN_CASCADE_COUNT);
    };
}

void SunShadowFeature::_cascade_bases(uint32_t p_cascade, uint32_t* r_base) const
{
    uint32_t base = 0;
    for (uint32_t k = 0; k < CASCADE_KINDS; k++) {
        r_base[k] = base;
        base += cascade_capacity[p_cascade][k];
    }
}

VkDeviceSize SunShadowFeature::_cascade_index_size(uint32_t p_cascade) const
{
    VkDeviceSize tris = 0;
    for (uint32_t k = 0; k < CASCADE_KINDS; k++) tris += cascade_capacity[p_cascade][k];
    return std::max<VkDeviceSize>(tris, 1) * 3 * sizeof(uint32_t);
}

VkDeviceSize SunShadowFeature::_cascade_material_size(uint32_t p_cascade) const
{
    return (VkDeviceSize)std::max(cascade_capacity[p_cascade][CASCADE_KIND_MIXED], 1u) * sizeof(uint32_t);
}

void SunShadowFeature::_cascade_capacity_update(uint32_t p_cascade)
{
    const uint32_t slot = ctx->graph->current_frame % (uint32_t)cascade_feedback.size();
    CascadeFeedback& fb = cascade_feedback[slot][p_cascade];
    if (!fb.written || !fb.buffer.mapped) return;
    fb.written = false;
    uint32_t words[CASCADE_FEEDBACK_WORDS] = {};
    ctx->dd->buffer_invalidate(fb.buffer, 0, sizeof(words));
    std::memcpy(words, fb.buffer.mapped, sizeof(words));
    for (uint32_t k = 0; k < CASCADE_KINDS; k++) {
        const uint64_t demand = words[k];
        cascade_demand[p_cascade][k] = words[k];
        cascade_spilled[p_cascade][k] = words[CASCADE_KINDS + k];
        uint32_t& capacity = cascade_capacity[p_cascade][k];
        uint64_t target = capacity;
        if (demand > capacity) {
            target = demand + demand / 2;
            cascade_low_frames[p_cascade][k] = 0;
        } else if (demand * 4 < capacity && capacity > CASCADE_KIND_INITIAL[k]) {
            if (++cascade_low_frames[p_cascade][k] >= CASCADE_SHRINK_FRAMES) {
                target = demand + demand / 2;
                cascade_low_frames[p_cascade][k] = 0;
            }
        } else {
            cascade_low_frames[p_cascade][k] = 0;
        }
        target = std::max<uint64_t>(target, CASCADE_KIND_INITIAL[k]);
        target = (target + CASCADE_KIND_GRANULE - 1) / CASCADE_KIND_GRANULE * CASCADE_KIND_GRANULE;
        capacity = (uint32_t)std::min<uint64_t>(target, CASCADE_KIND_MAX);
    }
}

bool SunShadowFeature::_cascade_frozen_alloc(uint32_t p_cascade)
{
    CascadeFrozen& f = cascade_frozen[p_cascade];
    const VkDeviceSize index_size = _cascade_index_size(p_cascade);
    const VkDeviceSize material_size = _cascade_material_size(p_cascade);
    if (f.indices.buffer && f.index_size < index_size) ctx->dd->buffer_free(f.indices);
    if (f.materials.buffer && f.material_size < material_size) ctx->dd->buffer_free(f.materials);
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!f.list.buffer) f.list = ctx->dd->buffer_create({ .size = CASCADE_LIST_SIZE, .usage = usage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, .device_local = true, .name = "sun_cascade_frozen_list" });
    if (!f.spill.buffer) f.spill = ctx->dd->buffer_create({ .size = CASCADE_SPILL_SIZE, .usage = usage, .device_local = true, .name = "sun_cascade_frozen_spill" });
    if (!f.indices.buffer) {
        f.indices = ctx->dd->buffer_create({ .size = index_size, .usage = usage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .device_local = true, .name = "sun_cascade_frozen_indices" });
        f.index_size = index_size;
    }
    if (!f.materials.buffer) {
        f.materials = ctx->dd->buffer_create({ .size = material_size, .usage = usage, .device_local = true, .name = "sun_cascade_frozen_materials" });
        f.material_size = material_size;
    }
    return f.list.buffer && f.indices.buffer && f.materials.buffer && f.spill.buffer;
}

void SunShadowFeature::_cascade_frozen_free()
{
    for (CascadeFrozen& f : cascade_frozen) {
        if (f.list.buffer) ctx->dd->buffer_free(f.list);
        if (f.indices.buffer) ctx->dd->buffer_free(f.indices);
        if (f.materials.buffer) ctx->dd->buffer_free(f.materials);
        if (f.spill.buffer) ctx->dd->buffer_free(f.spill);
        f = {};
    }
}

bool SunShadowFeature::_slot_stale(const Slot& p_slot) const
{
    const FrameData& f = *ctx->frame;
    return p_slot.mesh != f.player_mesh || p_slot.slot_base != f.player_slot_base || p_slot.lod_density != f.player_lod_density || p_slot.geometry_version != ctx->geometry->version || p_slot.residency_version != ctx->geometry->residency_version;
}

void SunShadowFeature::_slot_mark(Slot& r_slot) const
{
    const FrameData& f = *ctx->frame;
    r_slot.mesh = f.player_mesh;
    r_slot.slot_base = f.player_slot_base;
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
        s.materials = ctx->dd->buffer_create({ .size = (VkDeviceSize)PLAYER_SHADOW_MAX_TRIANGLES * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = true, .name = "player_shadow_materials" });
        LUMEN_ERR_FAIL_COND_V(!s.list.buffer || !s.indices.buffer || !s.materials.buffer, Error::FAILED);
    }
    drivers::DeviceDriverVulkan::SamplerCreateInfo sampler_ci{};
    sampler_ci.filter = VK_FILTER_LINEAR;
    sampler_ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.name = "player_shadow_material";
    material_sampler = ctx->dd->sampler_create(sampler_ci);
    LUMEN_ERR_FAIL_COND_V(!material_sampler.sampler, Error::FAILED);
    _create_reset_pass();
    _create_select_pass();
    _create_finalize_pass();
    _create_draw_pass();
    _create_skin_pass();

    cascade_work.resize(ctx->graph->frame_count);
    cascade_feedback.resize(ctx->graph->frame_count);
    for (auto& frame : cascade_feedback) {
        for (CascadeFeedback& fb : frame) {
            fb = {};
            fb.buffer = ctx->dd->buffer_create({ .size = CASCADE_FEEDBACK_WORDS * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = false, .host_visible = true, .cpu_read = true, .pool = ctx->dd->readback_pool, .name = "sun_cascade_feedback" });
            LUMEN_ERR_FAIL_COND_V(!fb.buffer.buffer, Error::FAILED);
        }
    }
    for (uint32_t c = 0; c < SUN_CASCADE_COUNT; c++) {
        for (uint32_t k = 0; k < CASCADE_KINDS; k++) {
            cascade_capacity[c][k] = CASCADE_KIND_INITIAL[k];
            cascade_low_frames[c][k] = 0;
        }
    }
    for (uint32_t c = 0; c < SUN_CASCADE_COUNT; c++) {
        _create_cascade_passes(c);
        if (c == 0) _create_receiver_passes();
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = cascade_names[c][NAME_IMAGE].c_str();
        ci.format = VK_FORMAT_D16_UNORM;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
        ci.fixed_width = SUN_CASCADE_SIZE;
        ci.fixed_height = SUN_CASCADE_SIZE;
        ci.pool = ctx->dd->image_persistent_pool;
        cascade_images[c] = ctx->dd->image_create_dedicated(ci, { SUN_CASCADE_SIZE, SUN_CASCADE_SIZE });
        LUMEN_ERR_FAIL_COND_V(!cascade_images[c].image, Error::FAILED);
    }
    for (uint32_t l = 0; l < HEIGHTFIELD_LEVELS; l++) {
        HeightfieldLevel& level = heightfield[l];
        level = {};
        level.name = "SunHeightfield" + std::to_string(l);
        level.old_name = "SunHeightfieldOld" + std::to_string(l);
        for (HeightfieldMap& m : level.maps) {
            drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
            ci.name = "sun_heightfield";
            ci.format = VK_FORMAT_R32_UINT;
            ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
            ci.fixed_width = SUN_HEIGHTFIELD_SIZE;
            ci.fixed_height = SUN_HEIGHTFIELD_SIZE;
            ci.pool = ctx->dd->image_persistent_pool;
            m.image = ctx->dd->image_create_dedicated(ci, { SUN_HEIGHTFIELD_SIZE, SUN_HEIGHTFIELD_SIZE });
            LUMEN_ERR_FAIL_COND_V(!m.image.image, Error::FAILED);
        }
    }
    heightfield_info.resize(ctx->graph->frame_count);
    for (drivers::DeviceDriverVulkan::Buffer& b : heightfield_info) {
        b = ctx->dd->buffer_create({ .size = sizeof(HeightfieldInfo), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = ctx->dd->bar_pool(), .name = "sun_heightfield_info" });
        LUMEN_ERR_FAIL_COND_V(!b.buffer, Error::FAILED);
    }
    _create_heightfield_pass();
    return Error::OK;
}

Error SunShadowFeature::create_pipelines()
{
    select_pipe = _compute_pipeline(L"SHADERS_LIGHTING_PLAYER_SHADOW_SELECT_COMP", "lighting/player_shadow_select.comp");
    finalize_pipe = _compute_pipeline(L"SHADERS_LIGHTING_PLAYER_SHADOW_FINALIZE_COMP", "lighting/player_shadow_finalize.comp");
    skin_pipe = _compute_pipeline(L"SHADERS_RASTER_SKIN_CLUSTERS_COMP", "raster/skin_clusters.comp");

    using Stage = drivers::DeviceDriverVulkan::ShaderStage;
    auto shader = [&](const wchar_t* p_resource, Stage p_stage, const char* p_name) {
        EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
        return ctx->dd->shader_create({ .stage = p_stage, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    };
    VkShaderModule vs = shader(L"SHADERS_LIGHTING_PLAYER_SHADOW_VERT", Stage::VERTEX, "lighting/player_shadow.vert");
    VkShaderModule masked_vs = shader(L"SHADERS_LIGHTING_PLAYER_SHADOW_MASKED_VERT", Stage::VERTEX, "lighting/player_shadow_masked.vert");
    VkShaderModule fs = shader(L"SHADERS_LIGHTING_PLAYER_SHADOW_FRAG", Stage::FRAGMENT, "lighting/player_shadow.frag");
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.render_pass = ctx->graph->acquire_render_pass(draw_pass);
    ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ci.depth_test = true;
    ci.depth_write = true;
    ci.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
    ci.vertex_shader = vs;
    ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    ci.name = "lighting/player_shadow";
    draw_pipe = ctx->dd->graphics_pipeline_create(ci);
    ci.vertex_shader = masked_vs;
    ci.fragment_shader = fs;
    ci.cull_mode = VK_CULL_MODE_NONE;
    ci.name = "lighting/player_shadow_masked";
    masked_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(masked_vs);
    ctx->dd->shader_free(fs);
    for (Slot& s : slots) s.mesh = UINT32_MAX;

    cascade_select_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_CASCADE_SELECT_COMP", "lighting/sun_cascade_select.comp");
    cascade_args_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_CASCADE_ARGS_COMP", "lighting/sun_cascade_args.comp");
    cascade_compact_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_CASCADE_COMPACT_COMP", "lighting/sun_cascade_compact.comp");
    receiver_mask_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_RECEIVER_MASK_COMP", "lighting/sun_receiver_mask.comp");
    receiver_pyramid_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_RECEIVER_PYRAMID_COMP", "lighting/sun_receiver_pyramid.comp");
    VkShaderModule cascade_vs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_VERT", Stage::VERTEX, "lighting/sun_cascade.vert");
    VkShaderModule cascade_masked_vs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_MASKED_VERT", Stage::VERTEX, "lighting/sun_cascade_masked.vert");
    VkShaderModule cascade_fs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_FRAG", Stage::FRAGMENT, "lighting/sun_cascade.frag");
    VkShaderModule cascade_mixed_fs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_MIXED_FRAG", Stage::FRAGMENT, "lighting/sun_cascade_mixed.frag");
    VkShaderModule cascade_spill_vs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_SPILL_VERT", Stage::VERTEX, "lighting/sun_cascade_spill.vert");
    VkShaderModule cascade_spill_masked_vs = shader(L"SHADERS_LIGHTING_SUN_CASCADE_SPILL_MASKED_VERT", Stage::VERTEX, "lighting/sun_cascade_spill_masked.vert");
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo cci{};
    for (uint32_t c = 0; c < SUN_CASCADE_COUNT; c++) ctx->graph->declare_image_format(cascade_names[c][NAME_IMAGE], VK_FORMAT_D16_UNORM);
    cci.render_pass = ctx->graph->acquire_render_pass(cascade_draw_pass[0]);
    cci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    cci.depth_test = true;
    cci.depth_write = true;
    cci.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
    cci.vertex_shader = cascade_vs;
    cci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    cci.name = "lighting/sun_cascade";
    cascade_opaque_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.cull_mode = VK_CULL_MODE_NONE;
    cci.name = "lighting/sun_cascade_two_sided";
    cascade_two_sided_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.vertex_shader = cascade_masked_vs;
    cci.fragment_shader = cascade_fs;
    cci.name = "lighting/sun_cascade_masked";
    cascade_masked_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.fragment_shader = cascade_mixed_fs;
    cci.name = "lighting/sun_cascade_masked_mixed";
    cascade_mixed_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.vertex_shader = cascade_spill_vs;
    cci.fragment_shader = VK_NULL_HANDLE;
    cci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    cci.name = "lighting/sun_cascade_spill";
    cascade_spill_opaque_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.cull_mode = VK_CULL_MODE_NONE;
    cci.name = "lighting/sun_cascade_spill_two_sided";
    cascade_spill_two_sided_pipe = ctx->dd->graphics_pipeline_create(cci);
    cci.vertex_shader = cascade_spill_masked_vs;
    cci.fragment_shader = cascade_fs;
    cci.name = "lighting/sun_cascade_spill_masked";
    cascade_spill_masked_pipe = ctx->dd->graphics_pipeline_create(cci);
    ctx->dd->shader_free(cascade_spill_vs);
    ctx->dd->shader_free(cascade_spill_masked_vs);
    ctx->dd->shader_free(cascade_vs);
    ctx->dd->shader_free(cascade_masked_vs);
    ctx->dd->shader_free(cascade_fs);
    ctx->dd->shader_free(cascade_mixed_fs);
    heightfield_pipe = _compute_pipeline(L"SHADERS_LIGHTING_SUN_HEIGHTFIELD_COMP", "lighting/sun_heightfield.comp");
    for (HeightfieldLevel& level : heightfield) {
        for (HeightfieldMap& m : level.maps) m.valid = false;
        level.building = false;
        level.blending = false;
    }
    return Error::OK;
}

void SunShadowFeature::destroy_resources()
{
    ctx->dd->pipeline_free(select_pipe);
    ctx->dd->pipeline_free(finalize_pipe);
    ctx->dd->pipeline_free(draw_pipe);
    ctx->dd->pipeline_free(masked_pipe);
    ctx->dd->pipeline_free(skin_pipe);
    skin_pipe = {};
    select_pipe = {};
    finalize_pipe = {};
    draw_pipe = {};
    masked_pipe = {};
    for (Slot& s : slots) {
        ctx->dd->buffer_free(s.list);
        ctx->dd->buffer_free(s.indices);
        ctx->dd->buffer_free(s.materials);
    }
    slots.clear();
    ctx->dd->sampler_free(material_sampler);
    material_sampler = {};

    ctx->dd->pipeline_free(cascade_select_pipe);
    ctx->dd->pipeline_free(cascade_args_pipe);
    ctx->dd->pipeline_free(cascade_compact_pipe);
    cascade_args_pipe = {};
    cascade_compact_pipe = {};
    ctx->dd->pipeline_free(receiver_mask_pipe);
    ctx->dd->pipeline_free(receiver_pyramid_pipe);
    receiver_mask_pipe = {};
    receiver_pyramid_pipe = {};
    ctx->dd->pipeline_free(cascade_opaque_pipe);
    ctx->dd->pipeline_free(cascade_masked_pipe);
    ctx->dd->pipeline_free(cascade_two_sided_pipe);
    ctx->dd->pipeline_free(cascade_mixed_pipe);
    cascade_two_sided_pipe = {};
    cascade_mixed_pipe = {};
    ctx->dd->pipeline_free(cascade_spill_opaque_pipe);
    ctx->dd->pipeline_free(cascade_spill_two_sided_pipe);
    ctx->dd->pipeline_free(cascade_spill_masked_pipe);
    cascade_spill_opaque_pipe = {};
    cascade_spill_two_sided_pipe = {};
    cascade_spill_masked_pipe = {};
    for (auto& frame : cascade_feedback) {
        for (CascadeFeedback& fb : frame) {
            if (fb.buffer.buffer) ctx->dd->buffer_free(fb.buffer);
            fb = {};
        }
    }
    cascade_feedback.clear();
    ctx->dd->pipeline_free(heightfield_pipe);
    cascade_select_pipe = {};
    cascade_opaque_pipe = {};
    cascade_masked_pipe = {};
    heightfield_pipe = {};
    for (auto& frame : cascade_work) {
        for (CascadeWork& w : frame) {
            if (w.buffer.buffer) ctx->dd->buffer_free(w.buffer);
            w = {};
        }
    }
    cascade_work.clear();
    _cascade_frozen_free();
    for (drivers::DeviceDriverVulkan::Image& image : cascade_images) {
        if (image.image) ctx->dd->image_free(image);
        image = {};
    }
    for (HeightfieldLevel& level : heightfield) {
        for (HeightfieldMap& m : level.maps) {
            if (m.image.image) ctx->dd->image_free(m.image);
            m = {};
        }
        level.building = false;
    }
    for (drivers::DeviceDriverVulkan::Buffer& b : heightfield_info) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    heightfield_info.clear();
}

void SunShadowFeature::build(RenderGraph& g)
{
    if (!ctx->frame || !ctx->geometry || slots.empty()) return;
    if (!g.buffer_resource("Instances") || !g.buffer_resource("Geometry")) return;
    _build_player(g);
    _build_cascades(g);
    _build_heightfield(g);
}

static bool _box_outside(const vec4* p_planes, uint32_t p_count, const vec3& p_min, const vec3& p_max)
{
    const vec3 center = (p_min + p_max) * 0.5f;
    const vec3 half = (p_max - p_min) * 0.5f;
    for (uint32_t p = 0; p < p_count; p++) {
        const vec3 n = vec3(p_planes[p]);
        if (dot(n, center) + p_planes[p].w < -dot(abs(n), half)) return true;
    }
    return false;
}

static void _box_world(const vec3& p_min, const vec3& p_extent, const mat4& p_model, vec3& r_min, vec3& r_max)
{
    const vec3 half = p_extent * 0.5f;
    const vec3 center = vec3(p_model * vec4(p_min + half, 1.0f));
    vec3 extent(0.0f);
    for (int r = 0; r < 3; r++) extent[r] = std::abs(p_model[0][r]) * half.x + std::abs(p_model[1][r]) * half.y + std::abs(p_model[2][r]) * half.z;
    r_min = center - extent;
    r_max = center + extent;
}

void SunShadowFeature::_cascade_casters(uint32_t p_cascade)
{
    FrameData& f = *ctx->frame;
    const vec4* planes = f.direct_light.cascade_planes[p_cascade];
    const uint32_t plane_count = f.direct_light.cascade_plane_count[p_cascade];
    const float texel = 2.0f * f.sun_cascade_sphere[p_cascade].w / (float)SUN_CASCADE_SIZE;
    const float min_radius = p_cascade == 0 ? 0.0f : texel * MIN_CASTER_TEXELS;
    work_scratch.clear();

    auto add = [&](uint32_t p_instance, uint32_t p_mesh) {
        const LMesh* mesh = ctx->geometry->get(p_mesh);
        if (!mesh) return;
        for (uint32_t o = 0; o < mesh->node_count; o += GROUP_SIZE) work_scratch.push_back(uvec2(p_instance, o));
    };

    const StaticInstances& st = f.static_insts;
    cull_scratch.clear();
    st.bvh.cull(planes, cull_scratch, plane_count);
    for (uint32_t i : cull_scratch) {
        if (length(st.bounds_max[i] - st.bounds_min[i]) * 0.5f < min_radius) continue;
        add(i, st.instances[i].mesh_id);
    }

    const uint32_t base = (uint32_t)st.instances.size();
    for (uint32_t j = 0; j < (uint32_t)f.instances_scratch.size(); j++) {
        const Instance& inst = f.instances_scratch[j];
        if (inst.flags & INSTANCE_FLAG_PLAYER) continue;
        const LMesh* mesh = ctx->geometry->get(inst.mesh_id);
        if (!mesh) continue;
        vec3 lo, hi;
        if (inst.skin_id != SKIN_NONE && inst.skin_id < f.skins_scratch.size()) {
            const SkinInstance& skin = f.skins_scratch[inst.skin_id];
            _box_world(skin.box_min, skin.box_extent, f.transforms_scratch[j].curr_mtx, lo, hi);
        } else {
            _box_world(mesh->pos_min, mesh->pos_extent, f.transforms_scratch[j].curr_mtx, lo, hi);
        }
        if (length(hi - lo) * 0.5f < min_radius || _box_outside(planes, plane_count, lo, hi)) continue;
        add(base + j, inst.mesh_id);
    }
}

bool SunShadowFeature::_cascade_upload(uint32_t p_cascade)
{
    CascadeWork& w = cascade_work[ctx->graph->current_frame % cascade_work.size()][p_cascade];
    const uint32_t count = (uint32_t)work_scratch.size();
    const VkDeviceSize bytes = (VkDeviceSize)std::max(count, 1u) * sizeof(uvec2);
    if (w.capacity < bytes) {
        if (w.buffer.buffer) ctx->dd->buffer_free(w.buffer);
        w.buffer = {};
        w.capacity = 0;
        const VkDeviceSize capacity = std::max<VkDeviceSize>(bytes + bytes / 2, 4096);
        w.buffer = ctx->dd->buffer_create({ .size = capacity, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .name = "sun_cascade_work" });
        if (!w.buffer.buffer) return false;
        w.capacity = capacity;
    }
    if (count) {
        ctx->dd->buffer_update(w.buffer, work_scratch.data(), (VkDeviceSize)count * sizeof(uvec2));
        ctx->dd->buffer_flush(w.buffer, 0, (VkDeviceSize)count * sizeof(uvec2));
    }
    cascade_work_count[p_cascade] = count;
    return true;
}

void SunShadowFeature::_build_cascades(RenderGraph& g)
{
    const FrameData& f = *ctx->frame;
    if (cascade_work.empty()) return;
    if (!freeze) {
        for (CascadeFrozen& fr : cascade_frozen) fr.ready = false;
    }
    const bool freeze_valid = f.instance_count >= frozen_instance_count;
    bool selecting = false;
    for (uint32_t c = 0; c < SUN_CASCADE_COUNT; c++) {
        if (f.direct_light.cascade_params[c].z == 0.0f || !cascade_images[c].image) continue;
        if (f.sun_cascade_reuse[c] && f.sun_cascade_history[c]) continue;
        if (freeze && cascade_frozen[c].ready && freeze_valid) continue;
        selecting = true;
    }
    if (selecting && g.image_resource("G_Depth")) {
        g.add(&receiver_clear_pass);
        g.add(&receiver_mask_pass);
        g.add(&receiver_pyramid_pass);
    }
    FrameData& frame = *ctx->frame;
    for (uint32_t c = 0; c < SUN_CASCADE_COUNT; c++) {
        cascade_replay[c] = false;
        if (f.direct_light.cascade_params[c].z == 0.0f || !cascade_images[c].image) {
            frame.sun_cascade_history[c] = false;
            continue;
        }
        auto import_image = [&]() {
            g.import_image(cascade_names[c][NAME_IMAGE], &cascade_images[c], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
        };
        if (f.sun_cascade_reuse[c] && frame.sun_cascade_history[c]) {
            import_image();
            continue;
        }
        frame.sun_cascade_history[c] = false;
        CascadeFrozen& fr = cascade_frozen[c];
        if (freeze && fr.ready && freeze_valid) {
            import_image();
            frame.sun_cascade_history[c] = true;
            g.import_buffer(cascade_names[c][NAME_FROZEN_LIST], &fr.list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_INDICES], &fr.indices, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_MATERIALS], &fr.materials, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_SPILL], &fr.spill, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            cascade_replay[c] = true;
            g.add(&cascade_draw_pass[c]);
            continue;
        }
        _cascade_capacity_update(c);
        _cascade_casters(c);
        if (!_cascade_upload(c)) continue;
        import_image();
        frame.sun_cascade_history[c] = true;
        CascadeWork& w = cascade_work[g.current_frame % cascade_work.size()][c];
        g.import_buffer(cascade_names[c][NAME_WORK], &w.buffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        g.add(&cascade_reset_pass[c]);
        if (cascade_work_count[c]) g.add(&cascade_select_pass[c]);
        g.add(&cascade_dispatch_args_pass[c]);
        g.add(&cascade_compact_pass[c]);
        g.add(&cascade_draw_args_pass[c]);
        if (!cascade_feedback.empty()) {
            CascadeFeedback& fb = cascade_feedback[g.current_frame % cascade_feedback.size()][c];
            g.import_buffer(cascade_names[c][NAME_FEEDBACK], &fb.buffer, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, true);
            g.add(&cascade_feedback_pass[c]);
            fb.written = true;
        }
        if (freeze && _cascade_frozen_alloc(c)) {
            g.import_buffer(cascade_names[c][NAME_FROZEN_LIST], &fr.list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_INDICES], &fr.indices, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_MATERIALS], &fr.materials, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer(cascade_names[c][NAME_FROZEN_SPILL], &fr.spill, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.add(&cascade_capture_pass[c]);
            fr.ready = true;
            frozen_instance_count = f.instance_count;
        }
        g.add(&cascade_draw_pass[c]);
    }
}

vec2 SunShadowFeature::_heightfield_axis(const vec3& p_dir)
{
    const float horizontal = length(vec2(p_dir.x, p_dir.z));
    return horizontal > 1e-4f ? -vec2(p_dir.x, p_dir.z) / horizontal : vec2(1.0f, 0.0f);
}

float SunShadowFeature::_heightfield_slope(const vec3& p_dir)
{
    return std::max(-p_dir.y, 0.0f) / std::max(length(vec2(p_dir.x, p_dir.z)), HEIGHTFIELD_MIN_HORIZONTAL);
}

float SunShadowFeature::_heightfield_texel(uint32_t p_level)
{
    return HEIGHTFIELD_EXTENT[p_level] / (float)SUN_HEIGHTFIELD_SIZE;
}

void SunShadowFeature::_create_heightfield_pass()
{
    heightfield_pass.name = "SunHeightfield";
    heightfield_pass.category = PASS_CATEGORY_SHADOWS;
    heightfield_pass.never_cull = true;
    heightfield_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("Terrain", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("TerrainHeights", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_image(heightfield_target_name, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    heightfield_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* terrain = cl.graph->buffer("Terrain");
        if (!terrain || !ctx->terrain || !cl.graph->image(heightfield_target_name)) return;
        const LTerrainPayloadHeader& h = ctx->terrain->source.header;
        const float range = std::max(h.height_max - h.height_min, 1e-3f);
        const float texel = _heightfield_texel(heightfield_exec_level);

        struct Push {
            VkDeviceAddress terrain_addr;
            vec2 center;
            vec2 sun;
            float texel;
            float drop;
            float height_min;
            float inv_range;
            uint32_t size;
            uint32_t prefix;
            uint32_t column_base;
            uint32_t skip;
            uint32_t out_slot;
            uint32_t coarse;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.terrain_addr = terrain->device_address;
        pc.center = heightfield_exec_center;
        pc.sun = _heightfield_axis(heightfield_exec_sun);
        pc.texel = texel;
        pc.drop = texel * _heightfield_slope(heightfield_exec_sun);
        pc.height_min = h.height_min;
        pc.inv_range = 1.0f / range;
        pc.size = SUN_HEIGHTFIELD_SIZE;
        pc.prefix = HEIGHTFIELD_PREFIX;
        pc.column_base = heightfield_exec_column;
        pc.skip = HEIGHTFIELD_SKIP[heightfield_exec_level];
        pc.out_slot = heightfield_exec_slot;
        pc.coarse = heightfield_exec_level == HEIGHTFIELD_LEVELS - 1 ? 1u : 0u;

        cl.dd->command_bind_pipeline(cl.cmd, heightfield_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Sun heightfield", heightfield_exec_columns);
    };
}

bool SunShadowFeature::_heightfield_dirty(uint32_t p_level, const vec2& p_center, const vec3& p_sun) const
{
    const HeightfieldLevel& level = heightfield[p_level];
    if (level.blending) return false;
    const HeightfieldMap& active = level.maps[level.active];
    if (!active.valid || active.origin != p_center || dot(active.sun, p_sun) < HEIGHTFIELD_SUN_COS[p_level] || active.terrain != ctx->terrain->source.guid) return true;
    if (p_level != 0) return false;
    return active.terrain_epoch != heightfield_epoch && heightfield_frame - heightfield_upload_frame >= HEIGHTFIELD_RESTREAM_FRAMES && heightfield_frame - level.build_frame >= HEIGHTFIELD_RESTREAM_FRAMES;
}

void SunShadowFeature::_heightfield_start(uint32_t p_level, const vec2& p_center, const vec3& p_sun)
{
    HeightfieldLevel& level = heightfield[p_level];
    HeightfieldMap& active = level.maps[level.active];
    const uint32_t target = active.valid ? 1u - level.active : level.active;
    HeightfieldMap& t = level.maps[target];
    t.origin = p_center;
    t.sun = p_sun;
    t.terrain = ctx->terrain->source.guid;
    t.terrain_epoch = heightfield_epoch;
    t.valid = false;
    level.building = true;
    level.column = 0;
    level.build_frame = heightfield_frame;
}

void SunShadowFeature::_build_heightfield(RenderGraph& g)
{
    FrameData& f = *ctx->frame;
    heightfield_frame++;
    if (heightfield_info.empty()) return;
    drivers::DeviceDriverVulkan::Buffer& info_buffer = heightfield_info[g.current_frame % heightfield_info.size()];
    HeightfieldInfo info{};

    const TerrainCache* terrain = ctx->terrain;
    const bool terrain_on = terrain && terrain->owner == ctx->frame && terrain->frame_active && g.buffer_resource("Terrain") && g.buffer_resource("TerrainHeights");
    const vec3 sun = normalize(f.direct_light.direction);
    const bool sun_up = -sun.y > HEIGHTFIELD_MIN_SUN_HEIGHT;

    if (f.sun_heightfield && terrain_on && sun_up) {
        if (!terrain->uploads.empty()) {
            heightfield_epoch++;
            heightfield_upload_frame = heightfield_frame;
        }
        for (HeightfieldLevel& level : heightfield) {
            if (level.blending && heightfield_frame - level.blend_start >= HEIGHTFIELD_BLEND_FRAMES) level.blending = false;
        }

        vec2 centers[HEIGHTFIELD_LEVELS];
        for (uint32_t l = 0; l < HEIGHTFIELD_LEVELS; l++) centers[l] = glm::round(vec2(f.camera.position.x, f.camera.position.z) / HEIGHTFIELD_RECENTER[l]) * HEIGHTFIELD_RECENTER[l];

        uint32_t build_level = HEIGHTFIELD_LEVELS;
        for (uint32_t l = 0; l < HEIGHTFIELD_LEVELS && build_level == HEIGHTFIELD_LEVELS; l++) {
            if (heightfield[l].building) build_level = l;
        }
        for (uint32_t l = 0; l < HEIGHTFIELD_LEVELS && build_level == HEIGHTFIELD_LEVELS; l++) {
            if (_heightfield_dirty(l, centers[l], sun)) {
                _heightfield_start(l, centers[l], sun);
                build_level = l;
            }
        }

        if (build_level != HEIGHTFIELD_LEVELS) {
            HeightfieldLevel& level = heightfield[build_level];
            const bool first = !level.maps[level.active].valid;
            const uint32_t target = first ? level.active : 1u - level.active;
            HeightfieldMap& map = level.maps[target];
            heightfield_exec_level = build_level;
            heightfield_exec_columns = first ? SUN_HEIGHTFIELD_SIZE : HEIGHTFIELD_COLUMNS_PER_FRAME[build_level];
            heightfield_exec_column = level.column;
            heightfield_exec_center = map.origin;
            heightfield_exec_sun = map.sun;
            heightfield_exec_slot = map.image.bindless_storage;
            const bool finish = level.column + heightfield_exec_columns >= SUN_HEIGHTFIELD_SIZE;
            if (finish) {
                if (!first) {
                    level.blending = true;
                    level.blend_start = heightfield_frame;
                }
                level.active = target;
                heightfield_target_name = level.name;
            } else {
                heightfield_target_name = "SunHeightfieldBuild";
            }
            g.import_image(heightfield_target_name, &map.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
            g.add(&heightfield_pass);
            level.column += heightfield_exec_columns;
            if (finish) {
                map.valid = true;
                level.building = false;
            }
        }

        info.params = vec4(terrain->source.header.height_min, std::max(terrain->source.header.height_max - terrain->source.header.height_min, 1e-3f), 0.0f, 0.0f);
        info.fade = vec4(HEIGHTFIELD_FADE[0][0], HEIGHTFIELD_FADE[0][1], HEIGHTFIELD_FADE[1][0], HEIGHTFIELD_FADE[1][1]);
        auto publish = [&](const std::string& p_name, HeightfieldMap& p_map, uint32_t p_level, uint32_t p_slot) {
            if (!g.image_resource(p_name)) g.import_image(p_name, &p_map.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, true);
            const float texel = _heightfield_texel(p_level);
            info.textures[p_slot] = p_map.image.bindless_sampled;
            info.area[p_slot] = vec4(p_map.origin, texel, 1.0f);
            info.axis[p_slot] = vec4(_heightfield_axis(p_map.sun), texel * HEIGHTFIELD_BIAS_TEXELS, texel * HEIGHTFIELD_NORMAL_OFFSET_TEXELS);
        };
        for (uint32_t l = 0; l < HEIGHTFIELD_LEVELS; l++) {
            HeightfieldLevel& level = heightfield[l];
            HeightfieldMap& shown = level.maps[level.active];
            if (!shown.valid) continue;
            publish(level.name, shown, l, 2 * l);
            HeightfieldMap& old = level.maps[1u - level.active];
            if (level.blending && old.valid) {
                publish(level.old_name, old, l, 2 * l + 1);
                info.blend[l] = 1.0f - (float)(heightfield_frame - level.blend_start) / (float)HEIGHTFIELD_BLEND_FRAMES;
            }
        }
    }

    ctx->dd->buffer_update(info_buffer, &info, sizeof(info));
    ctx->dd->buffer_flush(info_buffer, 0, sizeof(info));
    g.import_buffer("SunHeightfieldInfo", &info_buffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
}

void SunShadowFeature::_build_player(RenderGraph& g)
{
    const FrameData& f = *ctx->frame;
    if (f.player_instance == UINT32_MAX || f.player_mesh == UINT32_MAX || f.direct_light.player_shadow_params.z == 0.0f) return;
    const uvec4 rect = f.direct_light.player_shadow_rect;
    if (rect.z <= rect.x || rect.w <= rect.y) return;

    Slot& slot = slots[g.current_frame % slots.size()];
    g.import_buffer("PlayerShadowList", &slot.list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    g.import_buffer("PlayerShadowIndices", &slot.indices, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    g.import_buffer("PlayerShadowMaterials", &slot.materials, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    if (_slot_stale(slot)) {
        g.add(&reset_pass);
        g.add(&select_pass);
        g.add(&finalize_pass);
        _slot_mark(slot);
    }
    if (f.player_skin_table && f.skin_cache_header) g.add(&skin_pass);
    g.add(&draw_pass);
}

}
