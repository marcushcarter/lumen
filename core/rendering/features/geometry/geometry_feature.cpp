#include <core/rendering/features/geometry/geometry_feature.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/base/profiling.h>
#include <core/io/embedded_resource.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>
#include <cstring>
#include <algorithm>

namespace lumen {

using namespace glm;

void GeometryFeature::_compact_update(bool p_feedback, const uint64_t* p_demand)
{
    const uint64_t bound = std::min<uint64_t>((uint64_t)std::max(1u, ctx->frame->cluster_ref_capacity) * std::max(1u, ctx->geometry->max_cluster_tris), COMPACT_INDEX_LIMIT_TRIS);
    uint32_t target[RASTER_BIN_COUNT];
    uint64_t total = 0;
    bool changed = !compact_indices.buffer;
    for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
        const uint64_t capacity = compact_bin_capacity[b];
        uint64_t t = capacity;
        if (t == 0) t = b == RASTER_BIN_OPAQUE ? (uint64_t)ctx->graph->width * ctx->graph->height : 0;

        if (p_feedback) {
            const uint64_t demand = p_demand[b];
            if (demand > capacity) {
                t = demand + demand / 2;
                compact_low_frames[b] = 0;
            } else if (demand * 4 < capacity) {
                if (++compact_low_frames[b] >= COMPACT_SHRINK_FRAMES) {
                    t = demand + demand / 2;
                    compact_low_frames[b] = 0;
                }
            } else {
                compact_low_frames[b] = 0;
            }
        }

        t = std::max<uint64_t>(t, b == RASTER_BIN_OPAQUE ? COMPACT_MIN_TRIS : COMPACT_GRANULE_TRIS);
        t = (t + COMPACT_GRANULE_TRIS - 1) / COMPACT_GRANULE_TRIS * COMPACT_GRANULE_TRIS;
        t = std::min(t, bound);
        target[b] = (uint32_t)t;
        total += t;
        changed |= target[b] != compact_bin_capacity[b];
    }

    if (!changed || total > COMPACT_INDEX_LIMIT_TRIS) return;
    if (compact_failed_tris && total >= compact_failed_tris) return;

    if (!_compact_reserve(target)) {
        compact_failed_tris = total;
    } else {
        compact_failed_tris = 0;
        ++compact_resizes;
    }
}

bool GeometryFeature::_compact_reserve(const uint32_t* p_bin_tris)
{
    uint64_t total = 0;
    for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) total += p_bin_tris[b];
    const uint64_t masked = (uint64_t)p_bin_tris[RASTER_BIN_MASKED] + p_bin_tris[RASTER_BIN_MASKED | RASTER_BIN_TWO_SIDED];

    drivers::DeviceDriverVulkan::Buffer indices = ctx->dd->buffer_create({ .size = (VkDeviceSize)total * 3 * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .device_local = true, .name = "compact_indices" });
    drivers::DeviceDriverVulkan::Buffer prims = ctx->dd->buffer_create({ .size = (VkDeviceSize)total * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .device_local = true, .name = "compact_prims" });
    drivers::DeviceDriverVulkan::Buffer materials = ctx->dd->buffer_create({ .size = (VkDeviceSize)std::max<uint64_t>(masked, 1) * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .device_local = true, .name = "compact_materials" });
    if (!indices.buffer || !prims.buffer || !materials.buffer) {
        if (indices.buffer) ctx->dd->buffer_free(indices);
        if (prims.buffer) ctx->dd->buffer_free(prims);
        if (materials.buffer) ctx->dd->buffer_free(materials);
        return false;
    }

    _compact_retire(compact_indices);
    _compact_retire(compact_prims);
    _compact_retire(compact_materials);
    compact_indices = indices;
    compact_prims = prims;
    compact_materials = materials;

    uint32_t base = 0;
    for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
        compact_bin_base[b] = base;
        compact_bin_capacity[b] = p_bin_tris[b];
        base += p_bin_tris[b];
    }
    compact_capacity = base;
    return true;
}

void GeometryFeature::_compact_retire(drivers::DeviceDriverVulkan::Buffer& r_buffer)
{
    if (!r_buffer.buffer) return;
    compact_retired.push_back({ compact_serial, r_buffer });
    r_buffer = {};
}

void GeometryFeature::_compact_collect(bool p_all)
{
    const uint64_t frames = ctx->dd->frame_count;
    for (size_t i = 0; i < compact_retired.size();) {
        if (p_all || compact_serial - compact_retired[i].serial > frames) {
            ctx->dd->buffer_free(compact_retired[i].buffer);
            compact_retired[i] = compact_retired.back();
            compact_retired.pop_back();
        } else {
            ++i;
        }
    }
}

void GeometryFeature::_create_clear_visible_pass()
{
    clear_visible_pass.name = "ClearVisibleInstances";
    clear_visible_pass.category = PASS_CATEGORY_CULLING;
    clear_visible_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo visible_ci{};
        visible_ci.size = (VkDeviceSize)(ctx->frame->instance_count + 1) * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        visible_ci.device_local = true;
        b.create_buffer("VisibleInstances", visible_ci);
        b.create_buffer("OccludedInstances", visible_ci);
        b.create_buffer("VisibleInstances2", visible_ci);
        b.write_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.write_buffer("OccludedInstances", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.write_buffer("VisibleInstances2", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    clear_visible_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto visible = cl.graph->buffer("VisibleInstances");
        auto occluded = cl.graph->buffer("OccludedInstances");
        auto visible2 = cl.graph->buffer("VisibleInstances2");
        cl.fill_buffer("Clear visible instances", *visible, 0, 0, sizeof(uint32_t));
        cl.fill_buffer("Clear occluded instances", *occluded, 0, 0, sizeof(uint32_t));
        cl.fill_buffer("Clear visible instances 2", *visible2, 0, 0, sizeof(uint32_t));
    };
}

void GeometryFeature::_create_instance_cull_pass(RenderGraph::Pass& r_pass, bool p_late)
{
    r_pass.name = p_late ? "InstanceCull2" : "InstanceCull";
    r_pass.category = PASS_CATEGORY_CULLING;
    r_pass.setup = [p_late](RenderGraph::Builder& b) {
        b.read_image("HiZ", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (p_late) {
            b.read_buffer("OccludedInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            b.write_buffer("VisibleInstances2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        } else {
            b.write_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            b.write_buffer("OccludedInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
    };
    r_pass.execute = [this, p_late](RenderGraph::CommandList& cl) {
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto transforms = cl.graph->buffer("Transforms");
        auto visible = cl.graph->buffer(p_late ? "VisibleInstances2" : "VisibleInstances");
        auto occluded = cl.graph->buffer("OccludedInstances");
        auto hiz = cl.graph->image("HiZ");
        auto depth = cl.graph->image("G_Depth");
        auto selection = cl.graph->buffer("SelectionWords");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress occluded_addr;
            uint32_t instance_count;
            float px_per_unit;
            float min_screen_radius_px;
            uint32_t hiz_index;
            uint32_t hiz_mips;
            uint32_t hiz_enabled;
            uint32_t phase;
            uint32_t selection_enabled;
            float screen_size[2];
            VkDeviceAddress selection_addr;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.visible_addr = visible->device_address;
        pc.occluded_addr = occluded->device_address;
        pc.instance_count = ctx->frame->instance_count;
        pc.px_per_unit = ctx->frame->px_per_unit;
        pc.min_screen_radius_px = contribution_culling ? contribution_px : 0.0f;
        pc.hiz_index = hiz->bindless_sampled;
        pc.hiz_mips = hiz->mip_levels;
        pc.hiz_enabled = p_late ? ((occlusion_frame && hiz_ok) ? 1u : 0u) : (hiz_use_prev ? 1u : 0u);
        pc.phase = p_late ? 1u : 0u;
        pc.selection_enabled = selection ? 1u : 0u;
        pc.selection_addr = selection ? selection->device_address : 0;
        pc.screen_size[0] = (float)depth->extent.width;
        pc.screen_size[1] = (float)depth->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, instance_cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch(p_late ? "Instance cull 2" : "Instance cull", (ctx->frame->instance_count + 63) / 64);
    };
}

void GeometryFeature::_create_cluster_expand_args_pass(RenderGraph::Pass& r_pass, bool p_late)
{
    r_pass.name = p_late ? "ClusterExpandArgs2" : "ClusterExpandArgs";
    r_pass.category = PASS_CATEGORY_CULLING;
    r_pass.setup = [this, p_late](RenderGraph::Builder& b) {
        if (!p_late) {
            drivers::DeviceDriverVulkan::BufferCreateInfo args_ci{};
            args_ci.size = sizeof(IndirectDispatch);
            args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            args_ci.device_local = true;
            b.create_buffer("ClusterExpandArgs", args_ci);

            drivers::DeviceDriverVulkan::BufferCreateInfo refs_ci{};
            refs_ci.size = ((VkDeviceSize)ctx->frame->cluster_ref_capacity + 1) * sizeof(uint64_t);
            refs_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            refs_ci.device_local = true;
            b.create_buffer("ClusterRefs", refs_ci);

            drivers::DeviceDriverVulkan::BufferCreateInfo off_ci{};
            off_ci.size = (VkDeviceSize)(ctx->frame->instance_count + 1) * sizeof(uint32_t);
            off_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            off_ci.device_local = true;
            b.create_buffer("ClusterExpandOffsets", off_ci);
        }

        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(p_late ? "VisibleInstances2" : "VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterExpandOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterExpandArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (!p_late) b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_pass.execute = [this, p_late](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto vis_inst = cl.graph->buffer(p_late ? "VisibleInstances2" : "VisibleInstances");
        auto offsets = cl.graph->buffer("ClusterExpandOffsets");
        auto expand_args = cl.graph->buffer("ClusterExpandArgs");
        auto cluster_refs = cl.graph->buffer("ClusterRefs");

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress visible_inst_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress expand_addr;
            VkDeviceAddress cluster_refs_addr;
            uint32_t append;
        } pc;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.visible_inst_addr = vis_inst->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.expand_addr = expand_args->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.append = p_late ? 1u : 0u;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_expand_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch(p_late ? "Cluster expand args 2" : "Cluster expand args", 1);
    };
}

void GeometryFeature::_create_cluster_expand_pass(RenderGraph::Pass& r_pass, bool p_late)
{
    r_pass.name = p_late ? "ClusterExpand2" : "ClusterExpand";
    r_pass.category = PASS_CATEGORY_CULLING;
    r_pass.setup = [p_late](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(p_late ? "VisibleInstances2" : "VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterExpandOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterExpandArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (p_late) b.write_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_pass.execute = [this, p_late](RenderGraph::CommandList& cl) {
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto transforms = cl.graph->buffer("Transforms");
        auto visible = cl.graph->buffer(p_late ? "VisibleInstances2" : "VisibleInstances");
        auto offsets = cl.graph->buffer("ClusterExpandOffsets");
        auto expand_args = cl.graph->buffer("ClusterExpandArgs");
        auto cluster_refs = cl.graph->buffer("ClusterRefs");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress cluster_retest_addr;
            float px_per_unit;
            uint32_t late;
            uint32_t ref_capacity;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.visible_addr = visible->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.cluster_retest_addr = p_late ? cl.graph->buffer("ClusterRetest")->device_address : 0;
        pc.px_per_unit = ctx->frame->px_per_unit;
        pc.late = p_late ? 1u : 0u;
        pc.ref_capacity = ctx->frame->cluster_ref_capacity;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_expand_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect(p_late ? "Cluster expand 2" : "Cluster expand", *expand_args);
    };
}

void GeometryFeature::_create_cluster_cull_args_pass()
{
    cluster_cull_args_pass.name = "ClusterCullArgs";
    cluster_cull_args_pass.category = PASS_CATEGORY_CULLING;
    cluster_cull_args_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo args_ci{};
        args_ci.size = sizeof(IndirectDispatch);
        args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        args_ci.device_local = true;
        b.create_buffer("ClusterCullArgs", args_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo visible_ci{};
        visible_ci.size = ((VkDeviceSize)ctx->frame->cluster_ref_capacity + 1) * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        visible_ci.device_local = true;
        b.create_buffer("VisibleClusters", visible_ci);
        b.create_buffer("VisibleClusters2", visible_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo retest_ci{};
        retest_ci.size = ((VkDeviceSize)ctx->frame->cluster_ref_capacity + 1) * sizeof(uint32_t);
        retest_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        retest_ci.device_local = true;
        b.create_buffer("ClusterRetest", retest_ci);

        b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        if (compact_raster_frame) {
            drivers::DeviceDriverVulkan::BufferCreateInfo args_ci2{};
            args_ci2.size = COMPACT_ARGS_SIZE;
            args_ci2.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            args_ci2.device_local = true;
            b.create_buffer("CompactDrawArgs", args_ci2);
            b.write_buffer("CompactDrawArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            drivers::DeviceDriverVulkan::BufferCreateInfo spill_ci{};
            spill_ci.size = (VkDeviceSize)std::max(1u, ctx->frame->cluster_ref_capacity) * 2 * COMPACT_SPILL_CMD_SIZE;
            spill_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            spill_ci.device_local = true;
            b.create_buffer("CompactSpill", spill_ci);
        }
    };
    cluster_cull_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");
        auto vis_clus = cl.graph->buffer("VisibleClusters");
        auto vis_clus2 = cl.graph->buffer("VisibleClusters2");
        auto retest = cl.graph->buffer("ClusterRetest");

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress vis_clus_addr;
            VkDeviceAddress vis_clus2_addr;
            VkDeviceAddress retest_addr;
            VkDeviceAddress compact_args_addr;
            uint32_t ref_capacity;
            uint32_t init_compact;
        } pc{};
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.cull_addr = cull_args->device_address;
        pc.vis_clus_addr = vis_clus->device_address;
        pc.vis_clus2_addr = vis_clus2->device_address;
        pc.retest_addr = retest->device_address;
        pc.ref_capacity = ctx->frame->cluster_ref_capacity;
        if (auto* compact_args = cl.graph->buffer("CompactDrawArgs")) {
            pc.compact_args_addr = compact_args->device_address;
            pc.init_compact = 1u;
        }

        cl.dd->command_bind_pipeline(cl.cmd, cluster_cull_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Cluster cull args", 1);
    };
}

void GeometryFeature::_create_cluster_cull_pass()
{
    cluster_cull_pass.name = "ClusterCull";
    cluster_cull_pass.category = PASS_CATEGORY_CULLING;
    cluster_cull_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_image("HiZ", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cluster_cull_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto transforms = cl.graph->buffer("Transforms");
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");
        auto vis_clus = cl.graph->buffer("VisibleClusters");
        auto retest = cl.graph->buffer("ClusterRetest");
        auto hiz = cl.graph->image("HiZ");
        auto depth = cl.graph->image("G_Depth");

        CullPush pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.list_a_addr = vis_clus->device_address;
        pc.list_b_addr = retest->device_address;
        pc.hiz_index = hiz->bindless_sampled;
        pc.hiz_mips = hiz->mip_levels;
        pc.hiz_enabled = hiz_use_prev ? 1u : 0u;
        pc._pad = 0;
        pc.screen_size[0] = (float)depth->extent.width;
        pc.screen_size[1] = (float)depth->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster cull", *cull_args);
    };
}

void GeometryFeature::_create_hiz_pass(RenderGraph::Pass& r_build, const char* p_build_name, const char* p_args)
{
    r_build.name = p_build_name;
    r_build.category = PASS_CATEGORY_CULLING;
    r_build.setup = [p_args](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("HiZ", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("HiZTail", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (p_args && b.graph->buffer_resource(p_args)) b.read_buffer(p_args, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    };
    r_build.execute = [this, p_build_name, p_args](RenderGraph::CommandList& cl) {
        auto* hiz = cl.graph->image("HiZ");
        auto* depth = cl.graph->image("G_Depth");
        auto* tail = cl.graph->buffer("HiZTail");

        struct {
            VkDeviceAddress tail_addr;
            int32_t depth_size[2];
            uint32_t depth_index;
            uint32_t mips;
            uint32_t sampler_index;
            uint32_t _pad;
            uint32_t mip_slot[HIZ_MAX_MIPS];
        } pc{};
        pc.tail_addr = tail->device_address;
        pc.depth_size[0] = (int32_t)depth->extent.width;
        pc.depth_size[1] = (int32_t)depth->extent.height;
        pc.depth_index = depth->bindless_sampled;
        pc.mips = std::min(hiz->mip_levels, HIZ_MAX_MIPS);
        pc.sampler_index = hiz_sampler.bindless_sampler;
        for (uint32_t m = 0; m < pc.mips; m++) pc.mip_slot[m] = hiz->mip_storage_slots[m];

        cl.dd->command_bind_pipeline(cl.cmd, hiz_build_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        const drivers::DeviceDriverVulkan::Buffer* args = p_args ? cl.graph->buffer(p_args) : nullptr;
        if (args) cl.dispatch_indirect(p_build_name, *args, 0);
        else cl.dispatch(p_build_name, hiz->extent.width / HIZ_TILE, hiz->extent.height / HIZ_TILE);
    };
}

void GeometryFeature::_create_cluster_retest_args_pass()
{
    cluster_retest_args_pass.name = "ClusterRetestArgs";
    cluster_retest_args_pass.category = PASS_CATEGORY_CULLING;
    cluster_retest_args_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo args_ci{};
        args_ci.size = sizeof(IndirectDispatch);
        args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        args_ci.device_local = true;
        b.create_buffer("ClusterRetestArgs", args_ci);

        b.read_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        if (occlusion_frame && hiz_ok) {
            drivers::DeviceDriverVulkan::BufferCreateInfo hiz_args_ci{};
            hiz_args_ci.size = sizeof(uint32_t) * 3;
            hiz_args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            hiz_args_ci.device_local = true;
            b.create_buffer("HiZArgs2", hiz_args_ci);
            b.write_buffer("HiZArgs2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
    };
    cluster_retest_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto retest = cl.graph->buffer("ClusterRetest");
        auto args = cl.graph->buffer("ClusterRetestArgs");
        auto hiz_args = cl.graph->buffer("HiZArgs2");

        struct Push {
            VkDeviceAddress retest_addr;
            VkDeviceAddress args_addr;
            VkDeviceAddress hiz_args_addr;
            uint32_t init_hiz_args;
        } pc{};
        pc.retest_addr = retest->device_address;
        pc.args_addr = args->device_address;
        if (hiz_args) {
            pc.hiz_args_addr = hiz_args->device_address;
            pc.init_hiz_args = 1u;
        }

        cl.dd->command_bind_pipeline(cl.cmd, cluster_retest_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Cluster retest args", 1);
    };
}

void GeometryFeature::_create_cluster_retest_pass()
{
    cluster_retest_pass.name = "ClusterRetest";
    cluster_retest_pass.category = PASS_CATEGORY_CULLING;
    cluster_retest_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_image("HiZ", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cluster_retest_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto retest = cl.graph->buffer("ClusterRetest");
        auto vis2 = cl.graph->buffer("VisibleClusters2");
        auto args = cl.graph->buffer("ClusterRetestArgs");
        auto hiz = cl.graph->image("HiZ");
        auto depth = cl.graph->image("G_Depth");

        CullPush pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.list_a_addr = retest->device_address;
        pc.list_b_addr = vis2->device_address;
        pc.hiz_index = hiz->bindless_sampled;
        pc.hiz_mips = hiz->mip_levels;
        pc.hiz_enabled = (occlusion_frame && hiz_ok) ? 1u : 0u;
        pc._pad = 0;
        pc.screen_size[0] = (float)depth->extent.width;
        pc.screen_size[1] = (float)depth->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_retest_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster retest", *args);
    };
}

void GeometryFeature::_create_draw_compact_pass(RenderGraph::Pass& r_pass, bool p_late)
{
    r_pass.name = p_late ? "DrawCompact2" : "DrawCompact1";
    r_pass.category = PASS_CATEGORY_RASTER;
    r_pass.setup = [p_late](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(p_late ? "VisibleClusters2" : "VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(p_late ? "ClusterRetestArgs" : "ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("CompactIndices", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("CompactPrims", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("CompactMaterials", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("CompactDrawArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("CompactSpill", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (p_late && b.graph->buffer_resource("HiZArgs2")) b.write_buffer("HiZArgs2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_pass.execute = [this, p_late](RenderGraph::CommandList& cl) {
        auto* geometry = cl.graph->buffer("Geometry");
        auto* refs = cl.graph->buffer("ClusterRefs");
        auto* visible = cl.graph->buffer(p_late ? "VisibleClusters2" : "VisibleClusters");
        auto* dispatch_args = cl.graph->buffer(p_late ? "ClusterRetestArgs" : "ClusterCullArgs");
        auto* indices = cl.graph->buffer("CompactIndices");
        auto* prims = cl.graph->buffer("CompactPrims");
        auto* materials = cl.graph->buffer("CompactMaterials");
        auto* instances = cl.graph->buffer("Instances");
        auto* args = cl.graph->buffer("CompactDrawArgs");
        auto* hiz_args = p_late ? cl.graph->buffer("HiZArgs2") : nullptr;
        auto* hiz = cl.graph->image("HiZ");
        auto* spill = cl.graph->buffer("CompactSpill");
        if (!geometry || !instances || !refs || !visible || !dispatch_args || !args || !spill) return;

        if (!p_late) {
            VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            mb.srcStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &mb;
            vkCmdPipelineBarrier2(cl.cmd, &dep);
        }

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress indices_addr;
            VkDeviceAddress prims_addr;
            VkDeviceAddress materials_addr;
            VkDeviceAddress args_addr;
            VkDeviceAddress hiz_args_addr;
            VkDeviceAddress spill_addr;
            uint32_t region_base[RASTER_BIN_COUNT];
            uint32_t region_end;
            uint32_t phase;
            uint32_t hiz_groups_x;
            uint32_t hiz_groups_y;
            uint32_t spill_stride;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = instances->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.visible_addr = visible->device_address;
        pc.indices_addr = indices ? indices->device_address : 0;
        pc.prims_addr = prims ? prims->device_address : 0;
        pc.materials_addr = materials ? materials->device_address : 0;
        pc.args_addr = args->device_address;
        pc.spill_addr = spill->device_address;
        pc.phase = p_late ? 1u : 0u;
        if (indices && prims && materials) {
            for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) pc.region_base[b] = compact_bin_base[b];
            pc.region_end = compact_capacity;
        }
        pc.spill_stride = std::max(1u, ctx->frame->cluster_ref_capacity);
        if (hiz_args && hiz) {
            pc.hiz_args_addr = hiz_args->device_address;
            pc.hiz_groups_x = hiz->extent.width / HIZ_TILE;
            pc.hiz_groups_y = hiz->extent.height / HIZ_TILE;
        }

        cl.dd->command_bind_pipeline(cl.cmd, draw_compact_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect(p_late ? "Draw compact 2" : "Draw compact 1", *dispatch_args);
    };
}

void GeometryFeature::_create_visbuffer_compact_pass(RenderGraph::Pass& r_pass, bool p_late)
{
    r_pass.name = p_late ? "Visbuffer2" : "Visbuffer1";
    r_pass.category = PASS_CATEGORY_RASTER;
    r_pass.setup = [p_late](RenderGraph::Builder& b) {
        if (!p_late) {
            drivers::DeviceDriverVulkan::ImageCreateInfo depth_ci{};
            depth_ci.format = VK_FORMAT_D32_SFLOAT;
            depth_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            depth_ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            b.create_image("G_Depth", depth_ci);

            drivers::DeviceDriverVulkan::ImageCreateInfo vis_ci{};
            vis_ci.format = VK_FORMAT_R32_UINT;
            vis_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            vis_ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            b.create_image("G_Visibility", vis_ci);

            b.color_attachment("G_Visibility", VK_ATTACHMENT_LOAD_OP_CLEAR, VkClearValue{.color = {.uint32 = {VIS_EMPTY, 0u, 0u, 0u}}});
            b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_CLEAR, [] { VkClearValue v{}; v.depthStencil = { 0.0f, 0 }; return v; }());
        } else {
            b.color_attachment("G_Visibility", VK_ATTACHMENT_LOAD_OP_LOAD);
            b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_LOAD);
        }
        b.read_buffer("CompactIndices", VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
        b.read_buffer("CompactDrawArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("CompactPrims", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("CompactMaterials", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("CompactSpill", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer(p_late ? "VisibleClusters2" : "VisibleClusters", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    r_pass.execute = [this, p_late](RenderGraph::CommandList& cl) {
        auto* vis = cl.graph->image("G_Visibility");
        auto* indices = cl.graph->buffer("CompactIndices");
        auto* prims = cl.graph->buffer("CompactPrims");
        auto* materials = cl.graph->buffer("CompactMaterials");
        auto* args = cl.graph->buffer("CompactDrawArgs");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress prims_addr;
            VkDeviceAddress args_addr;
            VkDeviceAddress materials_addr;
            uint32_t phase;
            uint32_t bin;
            uint32_t sampler_index;
            uint32_t material_base;
        } pc{};
        pc.camera_addr = cl.graph->buffer("Camera")->device_address;
        pc.geometry_addr = cl.graph->buffer("Geometry")->device_address;
        pc.instances_addr = cl.graph->buffer("Instances")->device_address;
        pc.transforms_addr = cl.graph->buffer("Transforms")->device_address;
        pc.cluster_refs_addr = cl.graph->buffer("ClusterRefs")->device_address;
        pc.visible_addr = cl.graph->buffer(p_late ? "VisibleClusters2" : "VisibleClusters")->device_address;
        pc.prims_addr = prims ? prims->device_address : 0;
        pc.args_addr = args->device_address;
        pc.materials_addr = materials ? materials->device_address : 0;
        pc.phase = p_late ? 1u : 0u;
        pc.sampler_index = material_sampler.bindless_sampler;
        pc.material_base = compact_bin_base[RASTER_BIN_MASKED];

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, vis->extent }});

        if (indices && prims && materials) {
            cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
            for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
                pc.bin = b;
                cl.dd->command_bind_pipeline(cl.cmd, visbuffer_compact_pipes[b]);
                cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
                cl.draw_indexed_indirect(p_late ? "Visbuffer 2" : "Visbuffer 1", *args, (pc.phase * RASTER_BIN_COUNT + b) * COMPACT_CMD_SIZE, 1, COMPACT_CMD_SIZE);
            }
        }

        if (auto* spill = cl.graph->buffer("CompactSpill")) {
            struct SpillPush {
                VkDeviceAddress camera_addr;
                VkDeviceAddress geometry_addr;
                VkDeviceAddress instances_addr;
                VkDeviceAddress transforms_addr;
                VkDeviceAddress cluster_refs_addr;
                VkDeviceAddress visible_addr;
                uint32_t bin;
                uint32_t sampler_index;
            } sp{};
            sp.camera_addr = pc.camera_addr;
            sp.geometry_addr = pc.geometry_addr;
            sp.instances_addr = pc.instances_addr;
            sp.transforms_addr = pc.transforms_addr;
            sp.cluster_refs_addr = pc.cluster_refs_addr;
            sp.visible_addr = pc.visible_addr;
            sp.sampler_index = pc.sampler_index;

            const uint32_t stride = std::max(1u, ctx->frame->cluster_ref_capacity);
            for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
                sp.bin = b;
                cl.dd->command_bind_pipeline(cl.cmd, visbuffer_spill_pipes[b]);
                cl.dd->command_bind_push_constants(cl.cmd, sizeof(sp), &sp);
                cl.draw_indirect_count(p_late ? "Visbuffer 2 spill" : "Visbuffer 1 spill", *spill, (VkDeviceSize)pc.phase * stride * COMPACT_SPILL_CMD_SIZE, *args, COMPACT_ARGS_SPILL_OFFSET + pc.phase * sizeof(uint32_t), stride, COMPACT_SPILL_CMD_SIZE);
            }
        }
    };
}

static constexpr uint32_t RESOLVE_WRITE_MOTION = 1u << 0;
static constexpr uint32_t RESOLVE_WRITE_CUSTOM = 1u << 1;

void GeometryFeature::_create_material_resolve_pass()
{
    material_resolve_pass.name = "MaterialResolve";
    material_resolve_pass.category = PASS_CATEGORY_MATERIAL;
    material_resolve_pass.setup = [this](RenderGraph::Builder& b) {

        drivers::DeviceDriverVulkan::ImageCreateInfo normal_ci{};
        normal_ci.name = "G_Normal";
        normal_ci.format = VK_FORMAT_R16G16_UNORM;
        normal_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Normal", normal_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo albedo_ci{};
        albedo_ci.name = "G_Albedo";
        albedo_ci.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        albedo_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Albedo", albedo_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo material_ci{};
        material_ci.name = "G_Material";
        material_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        material_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Material", material_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo custom_ci{};
        custom_ci.name = "G_Custom";
        custom_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        custom_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Custom", custom_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo emissive_ci{};
        emissive_ci.name = "G_Emissive";
        emissive_ci.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        emissive_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Emissive", emissive_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo motion_ci{};
        motion_ci.name = "G_Motion";
        motion_ci.format = VK_FORMAT_R16G16_SFLOAT;
        motion_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Motion", motion_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo tiles_ci{};
        tiles_ci.size = SHADING_TILES_HEADER_SIZE + (VkDeviceSize)SHADING_TILE_TYPES * shading_tile_count(b.graph->width, b.graph->height) * sizeof(uint32_t);
        tiles_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        tiles_ci.device_local = true;
        b.create_buffer("ShadingTiles", tiles_ci);

        b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ShadingTiles", VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        b.write_image("G_Normal", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Albedo", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Material", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Custom", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Emissive", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Motion", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    material_resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto normal = cl.graph->image("G_Normal");
        auto albedo = cl.graph->image("G_Albedo");
        auto matl = cl.graph->image("G_Material");
        auto custom = cl.graph->image("G_Custom");
        auto emissive = cl.graph->image("G_Emissive");
        auto motion = cl.graph->image("G_Motion");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto tiles = cl.graph->buffer("ShadingTiles");
        if (!tiles) return;

        uint32_t gx = (vis->extent.width + 7) / 8;
        uint32_t gy = (vis->extent.height + 7) / 8;

        struct {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress shading_tiles_addr;
            uint32_t vis_index;
            uint32_t normal_slot;
            uint32_t albedo_slot;
            uint32_t material_slot;
            uint32_t custom_slot;
            uint32_t motion_slot;
            uint32_t emissive_slot;
            uint32_t width;
            uint32_t height;
            uint32_t flags;
            uint32_t sampler_index;
            float grad_scale;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.shading_tiles_addr = tiles->device_address;
        pc.vis_index = vis->bindless_sampled;
        pc.normal_slot = normal->bindless_storage;
        pc.albedo_slot = albedo->bindless_storage;
        pc.material_slot = matl->bindless_storage;
        pc.custom_slot = custom->bindless_storage;
        pc.motion_slot = motion->bindless_storage;
        pc.emissive_slot = emissive->bindless_storage;
        pc.width = vis->extent.width;
        pc.height = vis->extent.height;
        const RenderGraph::ImageResource* motion_res = cl.graph->image_resource("G_Motion");
        const RenderGraph::ImageResource* custom_res = cl.graph->image_resource("G_Custom");
        pc.flags = (motion_res && motion_res->read ? RESOLVE_WRITE_MOTION : 0u) | (custom_res && custom_res->read ? RESOLVE_WRITE_CUSTOM : 0u);
        pc.sampler_index = material_sampler.bindless_sampler;
        pc.grad_scale = ctx->frame->texture_grad_scale;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, vis->extent }});
        cl.fill_buffer("Shading tiles clear", *tiles, 0u, 0, SHADING_TILES_HEADER_SIZE);
        VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cl.cmd, &dep);

        cl.dd->command_bind_pipeline(cl.cmd, material_resolve_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        const uint32_t groups = gx * gy;
        cl.dispatch("Material resolve", std::min(groups, 65535u), (groups + 65534u) / 65535u);
    };
}

void GeometryFeature::_create_compact_feedback_pass()
{
    compact_feedback_pass.name = "CompactFeedback";
    compact_feedback_pass.category = PASS_CATEGORY_RASTER;
    compact_feedback_pass.never_cull = true;
    compact_feedback_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("CompactDrawArgs", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.write_buffer("CompactFeedbackReadback", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    compact_feedback_pass.execute = [this](RenderGraph::CommandList& cl) {
        const auto* args = cl.graph->buffer("CompactDrawArgs");
        if (!args) return;
        const drivers::DeviceDriverVulkan::Buffer& dst = compact_feedback[cl.graph->current_frame];
        cl.dd->command_copy_buffer(cl.cmd, *args, dst, sizeof(CompactFeedback), COMPACT_ARGS_TRIS_OFFSET, 0);

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
}

void GeometryFeature::_create_stats_pass()
{
    stats_pass.name = "ClusterCullStats";
    stats_pass.category = PASS_CATEGORY_PROFILING;
    stats_pass.never_cull = true;
    stats_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("OccludedInstances", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.read_buffer("VisibleInstances2", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.write_buffer("CullStatsReadback", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    stats_pass.execute = [this](RenderGraph::CommandList& cl) {
        const drivers::DeviceDriverVulkan::Buffer& dst = stats_readback[cl.graph->current_frame];
        const char* src_names[7] = { "ClusterRefs", "VisibleClusters", "ClusterRetest", "VisibleClusters2", "VisibleInstances", "OccludedInstances", "VisibleInstances2" };
        for (uint32_t i = 0; i < 7; i++) {
            if (const auto* src = cl.graph->buffer(src_names[i])) cl.dd->command_copy_buffer(cl.cmd, *src, dst, sizeof(uint32_t), 0, i * sizeof(uint32_t));
        }

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
}

Error GeometryFeature::create_resources()
{
    _create_clear_visible_pass();
    _create_instance_cull_pass(instance_cull_pass, false);
    _create_cluster_expand_args_pass(cluster_expand_args_pass, false);
    _create_cluster_expand_pass(cluster_expand_pass, false);
    _create_cluster_cull_args_pass();
    _create_cluster_cull_pass();
    _create_hiz_pass(hiz_build_pass, "HiZBuild1", nullptr);
    _create_instance_cull_pass(instance_cull_pass_2, true);
    _create_cluster_expand_args_pass(cluster_expand_args_pass_2, true);
    _create_cluster_expand_pass(cluster_expand_pass_2, true);
    _create_cluster_retest_args_pass();
    _create_cluster_retest_pass();
    _create_draw_compact_pass(draw_compact_pass, false);
    _create_draw_compact_pass(draw_compact_pass_2, true);
    _create_visbuffer_compact_pass(visbuffer_compact_pass, false);
    _create_visbuffer_compact_pass(visbuffer_compact_pass_2, true);
    _create_hiz_pass(hiz_build_pass_2, "HiZBuild2", "HiZArgs2");
    _create_material_resolve_pass();
    _create_stats_pass();
    _create_compact_feedback_pass();

    drivers::DeviceDriverVulkan::SamplerCreateInfo hiz_sampler_ci{};
    hiz_sampler_ci.filter = VK_FILTER_NEAREST;
    hiz_sampler_ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    hiz_sampler_ci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    hiz_sampler_ci.name = "hiz_gather";
    hiz_sampler = ctx->dd->sampler_create(hiz_sampler_ci);
    LUMEN_ERR_FAIL_COND_V_MSG(!hiz_sampler.sampler, Error::FAILED, "GeometryFeature: HiZ sampler creation failed.");

    drivers::DeviceDriverVulkan::SamplerCreateInfo material_sampler_ci{};
    material_sampler_ci.filter = VK_FILTER_LINEAR;
    material_sampler_ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    material_sampler_ci.address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    material_sampler_ci.anisotropy = 8.0f;
    material_sampler_ci.name = "material";
    material_sampler = ctx->dd->sampler_create(material_sampler_ci);
    LUMEN_ERR_FAIL_COND_V_MSG(!material_sampler.sampler, Error::FAILED, "GeometryFeature: material sampler creation failed.");

    const VkDeviceSize tail_size = sizeof(uint32_t) * 4 + sizeof(float) * HIZ_TAIL_MAX * HIZ_TAIL_MAX;
    hiz_tail_buffer = ctx->dd->buffer_create({ .size = tail_size, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .device_local = true, .name = "hiz_tail" });
    LUMEN_ERR_FAIL_COND_V_MSG(!hiz_tail_buffer.buffer, Error::FAILED, "GeometryFeature: HiZ tail buffer allocation failed.");
    const uint32_t zero_counter[4] = {};
    const drivers::DeviceDriverVulkan::BufferUpload tail_init{ &hiz_tail_buffer, zero_counter, sizeof(zero_counter), 0 };
    LUMEN_ERR_FAIL_COND_V_MSG(ctx->dd->buffer_upload_batch(&tail_init, 1) != Error::OK, Error::FAILED, "GeometryFeature: HiZ tail buffer init failed.");

    stats_readback.resize(ctx->dd->frame_count);
    stats_written.assign(ctx->dd->frame_count, 0);
    for (drivers::DeviceDriverVulkan::Buffer& b : stats_readback) {
        b = ctx->dd->buffer_create({ .size = sizeof(CullStats), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = false, .host_visible = true, .cpu_read = true, .pool = ctx->dd->readback_pool, .name = "cluster_cull_stats" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, Error::FAILED, "GeometryFeature: stats readback allocation failed.");
    }

    compact_feedback.resize(ctx->dd->frame_count);
    compact_feedback_written.assign(ctx->dd->frame_count, 0);
    for (drivers::DeviceDriverVulkan::Buffer& b : compact_feedback) {
        b = ctx->dd->buffer_create({ .size = sizeof(CompactFeedback), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = false, .host_visible = true, .cpu_read = true, .pool = ctx->dd->readback_pool, .name = "compact_feedback" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, Error::FAILED, "GeometryFeature: compact feedback readback allocation failed.");
    }
    return Error::OK;
};

Error GeometryFeature::create_pipelines()
{
    using enum Error;

    instance_cull_pipe = _compute_pipeline(L"SHADERS_CULLING_INSTANCE_CULL_COMP", "culling/instance_cull.comp");
    cluster_expand_args_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_EXPAND_ARGS_COMP", "culling/cluster_expand_args.comp");
    cluster_expand_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_EXPAND_COMP", "culling/cluster_expand.comp");
    cluster_cull_args_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_CULL_ARGS_COMP", "culling/cluster_cull_args.comp");
    cluster_cull_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_CULL_COMP", "culling/cluster_cull.comp");
    hiz_build_pipe = _compute_pipeline(L"SHADERS_CULLING_HIZ_BUILD_COMP", "culling/hiz_build.comp");
    cluster_retest_args_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_RETEST_ARGS_COMP", "culling/cluster_retest_args.comp");
    cluster_retest_pipe = _compute_pipeline(L"SHADERS_CULLING_CLUSTER_RETEST_COMP", "culling/cluster_retest.comp");
    material_resolve_pipe = _compute_pipeline(L"SHADERS_MATERIAL_MATERIAL_RESOLVE_COMP", "material/material_resolve.comp");
    draw_compact_pipe = _compute_pipeline(L"SHADERS_RASTER_DRAW_COMPACT_COMP", "raster/draw_compact.comp");

    {
    VkRenderPass rp = ctx->graph->acquire_render_pass(visbuffer_compact_pass);
    auto shader = [&](const wchar_t* p_resource, drivers::DeviceDriverVulkan::ShaderStage p_stage, const char* p_name) {
        EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
        return ctx->dd->shader_create({ .stage = p_stage, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    };
    using Stage = drivers::DeviceDriverVulkan::ShaderStage;
    VkShaderModule shaders[8] = {
        shader(L"SHADERS_RASTER_VISBUFFER_COMPACT_VERT", Stage::VERTEX, "raster/visbuffer_compact.vert"),
        shader(L"SHADERS_RASTER_VISBUFFER_COMPACT_FRAG", Stage::FRAGMENT, "raster/visbuffer_compact.frag"),
        shader(L"SHADERS_RASTER_VISBUFFER_COMPACT_MASKED_VERT", Stage::VERTEX, "raster/visbuffer_compact_masked.vert"),
        shader(L"SHADERS_RASTER_VISBUFFER_COMPACT_MASKED_FRAG", Stage::FRAGMENT, "raster/visbuffer_compact_masked.frag"),
        shader(L"SHADERS_RASTER_VISBUFFER_SPILL_VERT", Stage::VERTEX, "raster/visbuffer_spill.vert"),
        shader(L"SHADERS_RASTER_VISBUFFER_SPILL_FRAG", Stage::FRAGMENT, "raster/visbuffer_spill.frag"),
        shader(L"SHADERS_RASTER_VISBUFFER_SPILL_MASKED_VERT", Stage::VERTEX, "raster/visbuffer_spill_masked.vert"),
        shader(L"SHADERS_RASTER_VISBUFFER_SPILL_MASKED_FRAG", Stage::FRAGMENT, "raster/visbuffer_spill_masked.frag"),
    };
    static const char* COMPACT_NAMES[RASTER_BIN_COUNT] = { "raster/visbuffer_opaque", "raster/visbuffer_two_sided", "raster/visbuffer_masked", "raster/visbuffer_masked_two_sided" };
    static const char* SPILL_NAMES[RASTER_BIN_COUNT] = { "raster/visbuffer_spill_opaque", "raster/visbuffer_spill_two_sided", "raster/visbuffer_spill_masked", "raster/visbuffer_spill_masked_two_sided" };

    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.render_pass = rp;
    pipeline_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_ci.depth_test = true;
    pipeline_ci.depth_write = true;
    pipeline_ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
        const uint32_t variant = (b & RASTER_BIN_MASKED) ? 2u : 0u;
        pipeline_ci.cull_mode = (b & RASTER_BIN_TWO_SIDED) ? VK_CULL_MODE_NONE : VK_CULL_MODE_FRONT_BIT;
        pipeline_ci.vertex_shader = shaders[variant];
        pipeline_ci.fragment_shader = shaders[variant + 1];
        pipeline_ci.name = COMPACT_NAMES[b];
        visbuffer_compact_pipes[b] = ctx->dd->graphics_pipeline_create(pipeline_ci);
        pipeline_ci.vertex_shader = shaders[4 + variant];
        pipeline_ci.fragment_shader = shaders[4 + variant + 1];
        pipeline_ci.name = SPILL_NAMES[b];
        visbuffer_spill_pipes[b] = ctx->dd->graphics_pipeline_create(pipeline_ci);
    }
    for (VkShaderModule m : shaders) ctx->dd->shader_free(m);
    }

    return OK;
}

void GeometryFeature::destroy_resources()
{
    ctx->dd->pipeline_free(instance_cull_pipe);
    ctx->dd->pipeline_free(cluster_expand_args_pipe);
    ctx->dd->pipeline_free(cluster_expand_pipe);
    ctx->dd->pipeline_free(cluster_cull_args_pipe);
    ctx->dd->pipeline_free(cluster_cull_pipe);
    ctx->dd->pipeline_free(hiz_build_pipe);
    ctx->dd->pipeline_free(cluster_retest_args_pipe);
    ctx->dd->pipeline_free(cluster_retest_pipe);
    ctx->dd->pipeline_free(material_resolve_pipe);
    ctx->dd->pipeline_free(draw_compact_pipe);
    for (drivers::DeviceDriverVulkan::Pipeline& p : visbuffer_compact_pipes) ctx->dd->pipeline_free(p);
    for (drivers::DeviceDriverVulkan::Pipeline& p : visbuffer_spill_pipes) ctx->dd->pipeline_free(p);
    ctx->dd->sampler_free(hiz_sampler);
    ctx->dd->sampler_free(material_sampler);
    ctx->dd->buffer_free(hiz_tail_buffer);
    for (drivers::DeviceDriverVulkan::Buffer& b : stats_readback) ctx->dd->buffer_free(b);
    stats_readback.clear();
    stats_written.clear();
    for (drivers::DeviceDriverVulkan::Buffer& b : compact_feedback) ctx->dd->buffer_free(b);
    compact_feedback.clear();
    compact_feedback_written.clear();
    _compact_collect(true);
    ctx->dd->buffer_free(compact_indices);
    ctx->dd->buffer_free(compact_prims);
    ctx->dd->buffer_free(compact_materials);
    compact_capacity = 0;
    for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
        compact_bin_capacity[b] = 0;
        compact_bin_base[b] = 0;
        compact_low_frames[b] = 0;
    }
}

void GeometryFeature::build(RenderGraph& g)
{
    if (!enabled) {
        hiz_history = false;
        return;
    }

    const uint32_t slot = ctx->graph->current_frame;
    const bool want_stats = ctx->profiling && ctx->profiling->gpu_on();
    if (want_stats && slot < stats_readback.size() && stats_written[slot]) {
        drivers::DeviceDriverVulkan::Buffer& rb = stats_readback[slot];
        ctx->dd->buffer_invalidate(rb, 0, sizeof(CullStats));
        std::memcpy(&stats, rb.mapped, sizeof(CullStats));
    } else if (!want_stats) {
        stats = {};
    }

    const drivers::DeviceDriverVulkan::Image* hiz = g.image("HiZ");
    hiz_ok = hiz && hiz->image && hiz->mip_levels >= HIZ_TILE_MIPS && hiz->mip_levels <= 16 && hiz->extent.width % HIZ_TILE == 0 && hiz->extent.height % HIZ_TILE == 0 && hiz->extent.width / HIZ_TILE <= HIZ_TAIL_MAX && hiz->extent.height / HIZ_TILE <= HIZ_TAIL_MAX;
    occlusion_frame = occlusion;
    hiz_use_prev = occlusion_frame && hiz_ok && hiz_history && ctx->frame->hiz_history_valid;
    hiz_history = occlusion_frame && hiz_ok;

    constexpr bool MESH_PATH_READY = false;
    bool ms_active = MESH_PATH_READY && mesh_shading && ctx->dd->capabilities.mesh.mesh_shader;
    bool task_active = ms_active && task_shading && ctx->dd->capabilities.mesh.task_shader;
    compact_raster_frame = !ms_active;

    ++compact_serial;
    _compact_collect(false);
    if (compact_raster_frame) {
        bool feedback = false;
        uint64_t demand[RASTER_BIN_COUNT] = {};
        if (slot < compact_feedback.size() && compact_feedback_written[slot]) {
            CompactFeedback fb{};
            ctx->dd->buffer_invalidate(compact_feedback[slot], 0, sizeof(CompactFeedback));
            std::memcpy(&fb, compact_feedback[slot].mapped, sizeof(CompactFeedback));
            uint64_t total = 0;
            for (uint32_t b = 0; b < RASTER_BIN_COUNT; b++) {
                demand[b] = (uint64_t)fb.tris[b] + fb.tris[RASTER_BIN_COUNT + b];
                compact_bin_demand[b] = (uint32_t)std::min<uint64_t>(demand[b], UINT32_MAX);
                total += demand[b];
            }
            compact_demand = (uint32_t)std::min<uint64_t>(total, UINT32_MAX);
            compact_spilled = fb.spill[0] + fb.spill[1];
            feedback = true;
        }
        _compact_update(feedback, demand);
        if (compact_indices.buffer && compact_prims.buffer && compact_materials.buffer) {
            g.import_buffer("CompactIndices", &compact_indices, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            g.import_buffer("CompactPrims", &compact_prims, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            g.import_buffer("CompactMaterials", &compact_materials, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
    }

    g.add(&clear_visible_pass);
    g.add(&instance_cull_pass);
    g.add(&cluster_expand_args_pass);
    g.add(&cluster_expand_pass);
    g.add(&cluster_cull_args_pass);
    g.add(&cluster_cull_pass);
    if (task_active) {

    } else if (ms_active) {

    } else {
        g.add(&draw_compact_pass);
        g.add(&visbuffer_compact_pass);
    }
    if (occlusion_frame && hiz_ok) {
        g.import_buffer("HiZTail", &hiz_tail_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);
        g.add(&hiz_build_pass);
    }
    if (hiz_use_prev) {
        g.add(&instance_cull_pass_2);
        g.add(&cluster_expand_args_pass_2);
        g.add(&cluster_expand_pass_2);
    }
    g.add(&cluster_retest_args_pass);
    g.add(&cluster_retest_pass);
    if (task_active) {

    } else if (ms_active) {

    } else {
        g.add(&draw_compact_pass_2);
        g.add(&visbuffer_compact_pass_2);
        if (slot < compact_feedback.size()) {
            g.import_buffer("CompactFeedbackReadback", &compact_feedback[slot], VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, true);
            g.add(&compact_feedback_pass);
        }
    }
    if (slot < compact_feedback_written.size()) compact_feedback_written[slot] = compact_raster_frame ? 1 : 0;
    if (occlusion_frame && hiz_ok) g.add(&hiz_build_pass_2);
    g.add(&material_resolve_pass);
    if (slot < stats_readback.size()) {
        stats_written[slot] = want_stats ? 1 : 0;
        if (want_stats) {
            g.import_buffer("CullStatsReadback", &stats_readback[slot], VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, true);
            g.add(&stats_pass);
        }
    }
};

}
