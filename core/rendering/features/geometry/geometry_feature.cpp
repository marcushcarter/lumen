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

/******************/
/**** CLUSTERS ****/
/******************/

void GeometryFeature::_create_clear_visible_pass()
{
    clear_visible_pass.name = "ClearVisibleInstances";
    clear_visible_pass.category = PASS_CATEGORY_CULLING;
    clear_visible_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo visible_ci{};
        visible_ci.size = (VkDeviceSize)(ctx->frame->instance_count + 1) * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
            uint32_t _pad;
            float screen_size[2];
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
        pc.hiz_enabled = p_late ? ((occlusion && hiz_ok) ? 1u : 0u) : (hiz_use_prev ? 1u : 0u);
        pc.phase = p_late ? 1u : 0u;
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
            refs_ci.size = (VkDeviceSize)(ctx->frame->cluster_ref_capacity + 1) * sizeof(uint64_t);
            refs_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
        visible_ci.size = (VkDeviceSize)(ctx->frame->cluster_ref_capacity + 1) * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        visible_ci.device_local = true;
        b.create_buffer("VisibleClusters", visible_ci);
        b.create_buffer("VisibleClusters2", visible_ci);
        
        drivers::DeviceDriverVulkan::BufferCreateInfo retest_ci{};
        retest_ci.size = (VkDeviceSize)(ctx->frame->cluster_ref_capacity + 1) * sizeof(uint32_t);
        retest_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        retest_ci.device_local = true;
        b.create_buffer("ClusterRetest", retest_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo counts_ci{};
        counts_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        counts_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        counts_ci.device_local = true;
        b.create_buffer("ClusterCounts", counts_ci);
        
        b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    cluster_cull_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");
        auto vis_clus = cl.graph->buffer("VisibleClusters");
        auto vis_clus2 = cl.graph->buffer("VisibleClusters2");
        auto retest = cl.graph->buffer("ClusterRetest");
        auto counts = cl.graph->buffer("ClusterCounts");
        
        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress vis_clus_addr;
            VkDeviceAddress vis_clus2_addr;
            VkDeviceAddress retest_addr;
            uint32_t ref_capacity;
        } pc;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.cull_addr = cull_args->device_address;
        pc.vis_clus_addr = vis_clus->device_address;
        pc.vis_clus2_addr = vis_clus2->device_address;
        pc.retest_addr = retest->device_address;
        pc.ref_capacity = ctx->frame->cluster_ref_capacity;

        cl.fill_buffer("Clear cluster counts", *counts, 0);

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

void GeometryFeature::_create_draw_count_pass()
{
    draw_count_pass.name = "DrawCount1";
    draw_count_pass.category = PASS_CATEGORY_RASTER;
    draw_count_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    draw_count_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto refs = cl.graph->buffer("ClusterRefs");
        auto vis = cl.graph->buffer("VisibleClusters");
        auto counts = cl.graph->buffer("ClusterCounts");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_clusters_addr;
            VkDeviceAddress counts_addr;
        } pc;
        pc.cluster_refs_addr = refs->device_address;
        pc.visible_clusters_addr = vis->device_address;
        pc.counts_addr = counts->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, draw_count_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Draw count 1", *cull_args);
    };
}

void GeometryFeature::_create_draw_build_pass()
{
    draw_build_pass.name = "DrawBuild1";
    draw_build_pass.category = PASS_CATEGORY_RASTER;
    draw_build_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo offsets_ci{};
        offsets_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        offsets_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        offsets_ci.device_local = true;
        b.create_buffer("ClusterOffsets", offsets_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo cmds_ci{};
        cmds_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(VkDrawIndexedIndirectCommand);
        cmds_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        cmds_ci.device_local = true;
        b.create_buffer("ClusterDrawCmds", cmds_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo meta_ci{};
        meta_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        meta_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meta_ci.device_local = true;
        b.create_buffer("ClusterDrawMeta", meta_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo drawcount_ci{};
        drawcount_ci.size = sizeof(uint32_t);
        drawcount_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        drawcount_ci.device_local = true;
        b.create_buffer("ClusterDrawCount", drawcount_ci);

        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCmds", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawMeta", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCount", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    draw_build_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto counts = cl.graph->buffer("ClusterCounts");
        auto offsets = cl.graph->buffer("ClusterOffsets");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta");
        auto draw_count = cl.graph->buffer("ClusterDrawCount");

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress counts_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress draw_cmds_addr;
            VkDeviceAddress draw_meta_addr;
            VkDeviceAddress draw_count_addr;
            uint32_t cluster_count;
        } pc;
        pc.geometry_addr = geometry->device_address;
        pc.counts_addr = counts->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.draw_cmds_addr = draw_cmds->device_address;
        pc.draw_meta_addr = draw_meta->device_address;
        pc.draw_count_addr = draw_count->device_address;
        pc.cluster_count = ctx->geometry->cluster_extent;

        cl.dd->command_bind_pipeline(cl.cmd, draw_build_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Draw build 1", 1);
    };
}

void GeometryFeature::_create_draw_scatter_pass()
{
    draw_scatter_pass.name = "DrawScatter1";
    draw_scatter_pass.category = PASS_CATEGORY_RASTER;
    draw_scatter_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo scatter_ci{};
        scatter_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(uint32_t);
        scatter_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        scatter_ci.device_local = true;
        b.create_buffer("ClusterScatter", scatter_ci);

        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterScatter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    draw_scatter_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto refs = cl.graph->buffer("ClusterRefs");
        auto vis = cl.graph->buffer("VisibleClusters");
        auto offsets = cl.graph->buffer("ClusterOffsets");
        auto scatter = cl.graph->buffer("ClusterScatter");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_clusters_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress scatter_addr;
        } pc;
        pc.cluster_refs_addr = refs->device_address;
        pc.visible_clusters_addr = vis->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.scatter_addr = scatter->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, draw_scatter_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Draw scatter 1", *cull_args);
    };
}

void GeometryFeature::_create_visbuffer_pass()
{
    visbuffer_pass.name = "Visbuffer1";
    visbuffer_pass.category = PASS_CATEGORY_RASTER;
    visbuffer_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo depth_ci{};
        depth_ci.format = VK_FORMAT_D32_SFLOAT;
        depth_ci.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depth_ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        b.create_image("G_Depth", depth_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo vis_ci{};
        vis_ci.format = VK_FORMAT_R32G32_UINT;
        vis_ci.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        vis_ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        b.create_image("G_Visibility", vis_ci);

        b.color_attachment("G_Visibility", VK_ATTACHMENT_LOAD_OP_CLEAR, VkClearValue{.color = {.uint32 = {0u, 0u, 0u, 0u}}});      
        b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_CLEAR, [] { VkClearValue v{}; v.depthStencil = { 0.0f, 0 }; return v; }());
        b.read_buffer("ClusterDrawCmds", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("ClusterDrawCount", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterScatter", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterDrawMeta", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    visbuffer_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto scatter = cl.graph->buffer("ClusterScatter");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds");
        auto draw_count = cl.graph->buffer("ClusterDrawCount");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress scatter_addr;
            VkDeviceAddress draw_meta_addr;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.scatter_addr = scatter->device_address;
        pc.draw_meta_addr = draw_meta->device_address;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, vis->extent }});

        if (!ctx->geometry->index_buffer().buffer) return;
        cl.dd->command_bind_pipeline(cl.cmd, visbuffer_pipe);
        cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect_count("Visbuffer 1", *draw_cmds, 0, *draw_count, 0, ctx->geometry->cluster_extent, sizeof(VkDrawIndexedIndirectCommand));

        // cl.dd->command_bind_pipeline(cl.cmd, visbuffer_pipe);
        // cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        // cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        // cl.draw_indexed_indirect_count("Visbuffer 1", *draw_cmds, 0, *draw_count, 0, ctx->geometry->cluster_extent, sizeof(VkDrawIndexedIndirectCommand));
    };
}

void GeometryFeature::_create_hiz_passes(RenderGraph::Pass& r_build, RenderGraph::Pass& r_tail, const char* p_build_name, const char* p_tail_name)
{
    r_build.name = p_build_name;
    r_build.category = PASS_CATEGORY_CULLING;
    r_build.setup = [](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("HiZ", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_build.execute = [this, p_build_name](RenderGraph::CommandList& cl) {
        auto* hiz = cl.graph->image("HiZ");
        auto* depth = cl.graph->image("G_Depth");

        struct {
            int32_t depth_size[2];
            uint32_t depth_index;
            uint32_t mips;
            uint32_t mip_slot[HIZ_TILE_MIPS];
        } pc{};
        pc.depth_size[0] = (int32_t)depth->extent.width;
        pc.depth_size[1] = (int32_t)depth->extent.height;
        pc.depth_index = depth->bindless_sampled;
        pc.mips = hiz->mip_levels;
        for (uint32_t m = 0; m < HIZ_TILE_MIPS && m < hiz->mip_levels; m++) pc.mip_slot[m] = hiz->mip_storage_slots[m];

        cl.dd->command_bind_pipeline(cl.cmd, hiz_build_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch(p_build_name, hiz->extent.width / HIZ_TILE, hiz->extent.height / HIZ_TILE);
    };

    r_tail.name = p_tail_name;
    r_tail.category = PASS_CATEGORY_CULLING;
    r_tail.setup = [](RenderGraph::Builder& b) {
        b.write_image("HiZ", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    r_tail.execute = [this, p_tail_name](RenderGraph::CommandList& cl) {
        auto* hiz = cl.graph->image("HiZ");
        if (hiz->mip_levels <= HIZ_TILE_MIPS) return;

        struct {
            uint32_t mips;
            uint32_t _pad;
            int32_t size6[2];
            uint32_t mip_slot[16];
        } pc{};
        pc.mips = std::min(hiz->mip_levels, 16u);
        pc.size6[0] = (int32_t)(hiz->extent.width / HIZ_TILE);
        pc.size6[1] = (int32_t)(hiz->extent.height / HIZ_TILE);
        for (uint32_t m = 0; m < pc.mips; m++) pc.mip_slot[m] = hiz->mip_storage_slots[m];

        cl.dd->command_bind_pipeline(cl.cmd, hiz_tail_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch(p_tail_name, 1);
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

        drivers::DeviceDriverVulkan::BufferCreateInfo counts_ci{};
        counts_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        counts_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        counts_ci.device_local = true;
        b.create_buffer("ClusterCounts2", counts_ci);
        
        b.read_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    cluster_retest_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto retest = cl.graph->buffer("ClusterRetest");
        auto args = cl.graph->buffer("ClusterRetestArgs");
        auto counts = cl.graph->buffer("ClusterCounts2");
        
        struct Push {
            VkDeviceAddress retest_addr;
            VkDeviceAddress args_addr;
        } pc;
        pc.retest_addr = retest->device_address;
        pc.args_addr = args->device_address;

        cl.fill_buffer("Clear cluster counts 2", *counts, 0);

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
        pc.hiz_enabled = (occlusion && hiz_ok) ? 1u : 0u;
        pc._pad = 0;
        pc.screen_size[0] = (float)depth->extent.width;
        pc.screen_size[1] = (float)depth->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_retest_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster retest", *args);
    };
}

void GeometryFeature::_create_draw_count_2_pass()
{
    draw_count_pass_2.name = "DrawCount2";
    draw_count_pass_2.category = PASS_CATEGORY_RASTER;
    draw_count_pass_2.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    draw_count_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto refs = cl.graph->buffer("ClusterRefs");
        auto vis = cl.graph->buffer("VisibleClusters2");
        auto counts = cl.graph->buffer("ClusterCounts2");
        auto retest_args = cl.graph->buffer("ClusterRetestArgs");

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_clusters_addr;
            VkDeviceAddress counts_addr;
        } pc;
        pc.cluster_refs_addr = refs->device_address;
        pc.visible_clusters_addr = vis->device_address;
        pc.counts_addr = counts->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, draw_count_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Draw count 2", *retest_args);
    };
}

void GeometryFeature::_create_draw_build_2_pass()
{
    draw_build_pass_2.name = "DrawBuild2";
    draw_build_pass_2.category = PASS_CATEGORY_RASTER;
    draw_build_pass_2.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo offsets_ci{};
        offsets_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        offsets_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        offsets_ci.device_local = true;
        b.create_buffer("ClusterOffsets2", offsets_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo cmds_ci{};
        cmds_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(VkDrawIndexedIndirectCommand);
        cmds_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        cmds_ci.device_local = true;
        b.create_buffer("ClusterDrawCmds2", cmds_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo meta_ci{};
        meta_ci.size = (VkDeviceSize)(ctx->geometry->cluster_extent ? ctx->geometry->cluster_extent : 1u) * sizeof(uint32_t);
        meta_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meta_ci.device_local = true;
        b.create_buffer("ClusterDrawMeta2", meta_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo drawcount_ci{};
        drawcount_ci.size = sizeof(uint32_t);
        drawcount_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        drawcount_ci.device_local = true;
        b.create_buffer("ClusterDrawCount2", drawcount_ci);
    
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterOffsets2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCmds2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawMeta2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCount2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    
    };
    draw_build_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto counts = cl.graph->buffer("ClusterCounts2");
        auto offsets = cl.graph->buffer("ClusterOffsets2");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds2");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta2");
        auto draw_count = cl.graph->buffer("ClusterDrawCount2");

        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress counts_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress draw_cmds_addr;
            VkDeviceAddress draw_meta_addr;
            VkDeviceAddress draw_count_addr;
            uint32_t cluster_count;
        } pc;
        pc.geometry_addr = geometry->device_address;
        pc.counts_addr = counts->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.draw_cmds_addr = draw_cmds->device_address;
        pc.draw_meta_addr = draw_meta->device_address;
        pc.draw_count_addr = draw_count->device_address;
        pc.cluster_count = ctx->geometry->cluster_extent;

        cl.dd->command_bind_pipeline(cl.cmd, draw_build_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Draw build 2", 1);
    };
}

void GeometryFeature::_create_draw_scatter_2_pass()
{
    draw_scatter_pass_2.name = "DrawScatter2";
    draw_scatter_pass_2.category = PASS_CATEGORY_RASTER;
    draw_scatter_pass_2.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo scatter_ci{};
        scatter_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(uint32_t);
        scatter_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        scatter_ci.device_local = true;
        b.create_buffer("ClusterScatter2", scatter_ci);

        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterOffsets2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterScatter2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    
    };
    draw_scatter_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto refs = cl.graph->buffer("ClusterRefs");
        auto vis = cl.graph->buffer("VisibleClusters2");
        auto offsets = cl.graph->buffer("ClusterOffsets2");
        auto scatter = cl.graph->buffer("ClusterScatter2");
        auto retest_args = cl.graph->buffer("ClusterRetestArgs");

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_clusters_addr;
            VkDeviceAddress offsets_addr;
            VkDeviceAddress scatter_addr;
        } pc;
        pc.cluster_refs_addr = refs->device_address;
        pc.visible_clusters_addr = vis->device_address;
        pc.offsets_addr = offsets->device_address;
        pc.scatter_addr = scatter->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, draw_scatter_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Draw scatter 2", *retest_args);
    };
}

void GeometryFeature::_create_visbuffer_2_pass()
{
    visbuffer_pass_2.name = "Visbuffer2";
    visbuffer_pass_2.category = PASS_CATEGORY_RASTER;
    visbuffer_pass_2.setup = [this](RenderGraph::Builder& b) {
        b.color_attachment("G_Visibility", VK_ATTACHMENT_LOAD_OP_LOAD);
        b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_LOAD);
        b.read_buffer("ClusterDrawCmds2", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("ClusterDrawCount2", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterScatter2", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterDrawMeta2", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    visbuffer_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto scatter = cl.graph->buffer("ClusterScatter2");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta2");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds2");
        auto draw_count = cl.graph->buffer("ClusterDrawCount2");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress scatter_addr;
            VkDeviceAddress draw_meta_addr;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.scatter_addr = scatter->device_address;
        pc.draw_meta_addr = draw_meta->device_address;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, vis->extent }});

        if (!ctx->geometry->index_buffer().buffer) return;
        cl.dd->command_bind_pipeline(cl.cmd, visbuffer_pipe);
        cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect_count("Visbuffer 2", *draw_cmds, 0, *draw_count, 0, ctx->geometry->cluster_extent, sizeof(VkDrawIndexedIndirectCommand));

        // cl.dd->command_bind_pipeline(cl.cmd, visbuffer_pipe);
        // cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        // cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        // cl.draw_indexed_indirect_count("Visbuffer 2", *draw_cmds, 0, *draw_count, 0, ctx->geometry->cluster_extent, sizeof(VkDrawIndexedIndirectCommand));
    };
}

/******************/
/**** MATERIAL ****/
/******************/

void GeometryFeature::_create_material_resolve_pass()
{
    material_resolve_pass.name = "MaterialResolve";
    material_resolve_pass.category = PASS_CATEGORY_MATERIAL;
    material_resolve_pass.never_cull = true;
    material_resolve_pass.setup = [this](RenderGraph::Builder& b) {
        
        drivers::DeviceDriverVulkan::ImageCreateInfo normal_ci{};
        normal_ci.name = "G_Normal";
        normal_ci.format = VK_FORMAT_R16G16_UNORM;
        normal_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Normal", normal_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo albedo_ci{};
        albedo_ci.name = "G_Albedo";
        albedo_ci.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        albedo_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Albedo", albedo_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo material_ci{};
        material_ci.name = "G_Material";
        material_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        material_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Material", material_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo custom_ci{};
        custom_ci.name = "G_Custom";
        custom_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        custom_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Custom", custom_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo motion_ci{};
        motion_ci.name = "G_Motion";
        motion_ci.format = VK_FORMAT_R16G16_SFLOAT;
        motion_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Motion", motion_ci);

        b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        b.write_image("G_Normal", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Albedo", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Material", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Custom", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_image("G_Motion", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    material_resolve_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto depth = cl.graph->image("G_Depth");
        auto normal = cl.graph->image("G_Normal");
        auto albedo = cl.graph->image("G_Albedo");
        auto matl = cl.graph->image("G_Material");
        auto custom = cl.graph->image("G_Custom");
        auto motion = cl.graph->image("G_Motion");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        
        uint32_t gx = (vis->extent.width + 7) / 8;
        uint32_t gy = (vis->extent.height + 7) / 8;

        struct {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            uint32_t vis_index;
            uint32_t depth_index;
            uint32_t normal_slot;
            uint32_t albedo_slot;
            uint32_t material_slot;
            uint32_t custom_slot;
            uint32_t motion_slot;
            uint32_t width;
            uint32_t height;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.vis_index = vis->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.normal_slot = normal->bindless_storage;
        pc.albedo_slot = albedo->bindless_storage;
        pc.material_slot = matl->bindless_storage;
        pc.custom_slot = custom->bindless_storage;
        pc.motion_slot = motion->bindless_storage;
        pc.width = vis->extent.width;
        pc.height = vis->extent.height;

        cl.dd->command_render_set_viewport(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0,0}, vis->extent }});
        cl.dd->command_bind_pipeline(cl.cmd, material_resolve_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Material resolve", gx, gy);
    };
}

/*******************/
/**** PROFILING ****/
/*******************/

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
    };
    stats_pass.execute = [this](RenderGraph::CommandList& cl) {
        const drivers::DeviceDriverVulkan::Buffer& dst = stats_readback[cl.graph->current_frame];
        const char* src_names[7] = { "ClusterRefs", "VisibleClusters", "ClusterRetest", "VisibleClusters2", "VisibleInstances", "OccludedInstances", "VisibleInstances2" };
        for (uint32_t i = 0; i < 7; i++) cl.dd->command_copy_buffer(cl.cmd, *cl.graph->buffer(src_names[i]), dst, sizeof(uint32_t), 0, i * sizeof(uint32_t));

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

/**************/
/**** BASE ****/
/**************/

Error GeometryFeature::create_resources()
{
    _create_clear_visible_pass();
    _create_instance_cull_pass(instance_cull_pass, false);
    _create_cluster_expand_args_pass(cluster_expand_args_pass, false);
    _create_cluster_expand_pass(cluster_expand_pass, false);
    _create_cluster_cull_args_pass();
    _create_cluster_cull_pass();
    _create_draw_count_pass();
    _create_draw_build_pass();
    _create_draw_scatter_pass();
    _create_visbuffer_pass();
    _create_hiz_passes(hiz_build_pass, hiz_tail_pass, "HiZBuild1", "HiZTail1");
    _create_instance_cull_pass(instance_cull_pass_2, true);
    _create_cluster_expand_args_pass(cluster_expand_args_pass_2, true);
    _create_cluster_expand_pass(cluster_expand_pass_2, true);
    _create_cluster_retest_args_pass();
    _create_cluster_retest_pass();
    _create_draw_count_2_pass();
    _create_draw_build_2_pass();
    _create_draw_scatter_2_pass();
    _create_visbuffer_2_pass();
    _create_hiz_passes(hiz_build_pass_2, hiz_tail_pass_2, "HiZBuild2", "HiZTail2");
    _create_material_resolve_pass();
    _create_stats_pass();

    stats_readback.resize(ctx->dd->frame_count);
    stats_written.assign(ctx->dd->frame_count, 0);
    for (drivers::DeviceDriverVulkan::Buffer& b : stats_readback) {
        b = ctx->dd->buffer_create({ .size = sizeof(CullStats), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = false, .host_visible = true, .cpu_read = true, .pool = ctx->dd->readback_pool, .name = "cluster_cull_stats" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, Error::FAILED, "GeometryFeature: stats readback allocation failed.");
    }
    return Error::OK;
};

Error GeometryFeature::create_pipelines()
{
    using enum Error;

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_INSTANCE_CULL_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/instance_cull.comp" });
    instance_cull_pipe = ctx->dd->compute_pipeline_create({cs, "culling/instance_cull"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_EXPAND_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_expand_args.comp" });
    cluster_expand_args_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_expand_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_EXPAND_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_expand.comp" });
    cluster_expand_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_expand"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_CULL_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_cull_args.comp" });
    cluster_cull_args_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_cull_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_CULL_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_cull.comp" });
    cluster_cull_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_cull"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_RASTER_DRAW_COUNT_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "raster/draw_count.comp" });
    draw_count_pipe = ctx->dd->compute_pipeline_create({cs, "raster/draw_count"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_RASTER_DRAW_BUILD_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "raster/draw_build.comp" });
    draw_build_pipe = ctx->dd->compute_pipeline_create({cs, "raster/draw_build"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_RASTER_DRAW_SCATTER_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "raster/draw_scatter.comp" });
    draw_scatter_pipe = ctx->dd->compute_pipeline_create({cs, "raster/draw_scatter"});
    ctx->dd->shader_free(cs);
    }

    {
    VkRenderPass rp = ctx->graph->acquire_render_pass(visbuffer_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_RASTER_VISBUFFER_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_RASTER_VISBUFFER_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX,   .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "raster/visbuffer.vert" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "raster/visbuffer.frag" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.vertex_shader = vs; pipeline_ci.fragment_shader = fs; pipeline_ci.render_pass = rp;
    pipeline_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    pipeline_ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_ci.depth_test = true;
    pipeline_ci.depth_write = true;
    pipeline_ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    pipeline_ci.name = "raster/visbuffer";
    visbuffer_pipe = ctx->dd->graphics_pipeline_create(pipeline_ci);
    ctx->dd->shader_free(vs); ctx->dd->shader_free(fs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_HIZ_BUILD_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/hiz_build.comp" });
    hiz_build_pipe = ctx->dd->compute_pipeline_create({cs, "culling/hiz_build"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_HIZ_TAIL_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/hiz_tail.comp" });
    hiz_tail_pipe = ctx->dd->compute_pipeline_create({cs, "culling/hiz_tail"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_RETEST_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_retest_args.comp" });
    cluster_retest_args_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_retest_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CULLING_CLUSTER_RETEST_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "culling/cluster_retest.comp" });
    cluster_retest_pipe = ctx->dd->compute_pipeline_create({cs, "culling/cluster_retest"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_MATERIAL_MATERIAL_RESOLVE_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "material/material_resolve.comp" });
    material_resolve_pipe = ctx->dd->compute_pipeline_create({cs, "material/material_resolve"});
    ctx->dd->shader_free(cs);
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
    ctx->dd->pipeline_free(draw_count_pipe);
    ctx->dd->pipeline_free(draw_build_pipe);
    ctx->dd->pipeline_free(draw_scatter_pipe);
    ctx->dd->pipeline_free(visbuffer_pipe);
    ctx->dd->pipeline_free(hiz_build_pipe);
    ctx->dd->pipeline_free(hiz_tail_pipe);
    ctx->dd->pipeline_free(cluster_retest_args_pipe);
    ctx->dd->pipeline_free(cluster_retest_pipe);
    ctx->dd->pipeline_free(material_resolve_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : stats_readback) ctx->dd->buffer_free(b);
    stats_readback.clear();
    stats_written.clear();
}

void GeometryFeature::build(RenderGraph& g)
{
    if (!enabled || !ctx->geometry->allocated) {
        hiz_history = false;
        return;
    }

    const uint32_t slot = ctx->graph->current_frame;
    const bool want_stats = ctx->profiling && ctx->profiling->cull_stats_on();
    if (want_stats && slot < stats_readback.size() && stats_written[slot]) {
        drivers::DeviceDriverVulkan::Buffer& rb = stats_readback[slot];
        ctx->dd->buffer_invalidate(rb, 0, sizeof(CullStats));
        std::memcpy(&stats, rb.mapped, sizeof(CullStats));
    } else if (!want_stats) {
        stats = {};
    }

    const drivers::DeviceDriverVulkan::Image* hiz = g.image("HiZ");
    hiz_ok = hiz && hiz->image && hiz->mip_levels >= HIZ_TILE_MIPS && hiz->mip_levels <= 16 && hiz->extent.width % HIZ_TILE == 0 && hiz->extent.height % HIZ_TILE == 0 && hiz->extent.width / HIZ_TILE <= HIZ_TAIL_MAX && hiz->extent.height / HIZ_TILE <= HIZ_TAIL_MAX;
    hiz_use_prev = occlusion && hiz_ok && hiz_history && ctx->frame->hiz_history_valid;
    hiz_history = occlusion && hiz_ok;

    bool ms_active = mesh_shading && ctx->dd->capabilities.mesh.mesh_shader;
    bool task_active = ms_active && task_shading && ctx->dd->capabilities.mesh.task_shader;

    g.add(&clear_visible_pass);
    g.add(&instance_cull_pass);
    g.add(&cluster_expand_args_pass);
    g.add(&cluster_expand_pass);
    g.add(&cluster_cull_args_pass);
    g.add(&cluster_cull_pass);
    if (task_active) {

    } if (ms_active) {

    } else {
        g.add(&draw_count_pass);
        g.add(&draw_build_pass);
        g.add(&draw_scatter_pass);
        g.add(&visbuffer_pass);
    }
    if (occlusion && hiz_ok) {
        g.add(&hiz_build_pass);
        g.add(&hiz_tail_pass);
    }
    if (hiz_use_prev) {
        g.add(&instance_cull_pass_2);
        g.add(&cluster_expand_args_pass_2);
        g.add(&cluster_expand_pass_2);
    }
    g.add(&cluster_retest_args_pass);
    g.add(&cluster_retest_pass);
    if (task_active) {

    } if (ms_active) {

    } else {
        g.add(&draw_count_pass_2);
        g.add(&draw_build_pass_2);
        g.add(&draw_scatter_pass_2);
        g.add(&visbuffer_pass_2);   
    }
    if (occlusion && hiz_ok) {
        g.add(&hiz_build_pass_2);
        g.add(&hiz_tail_pass_2);
    }
    g.add(&material_resolve_pass);
    if (slot < stats_readback.size()) {
        stats_written[slot] = want_stats ? 1 : 0;
        if (want_stats) g.add(&stats_pass);
    }
};

}
