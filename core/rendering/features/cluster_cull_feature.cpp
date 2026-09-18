#include <core/rendering/features/cluster_cull_feature.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

void ClusterCullFeature::_create_clear_visible_pass()
{
    clear_visible_pass.name = "ClearVisibleInstances";
    clear_visible_pass.category = "ClusterCull";
    clear_visible_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo visible_ci{};
        visible_ci.size = (VkDeviceSize)(ctx->frame->instance_count + 1) * sizeof(uint32_t);
        visible_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        visible_ci.device_local = true;
        b.create_buffer("VisibleInstances", visible_ci);
        b.write_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    clear_visible_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto visible = cl.graph->buffer("VisibleInstances");
        cl.fill_buffer("Clear visible instances", *visible, 0, 0, sizeof(uint32_t));
    };
}

void ClusterCullFeature::_create_instance_cull_pass()
{
    instance_cull_pass.name = "InstanceCull";
    instance_cull_pass.category = "ClusterCull";
    instance_cull_pass.setup = [](RenderGraph::Builder& b) {        
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    instance_cull_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto tranforms = cl.graph->buffer("Transforms");
        auto visible = cl.graph->buffer("VisibleInstances");

        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress visible_addr;
            uint32_t instance_count;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = tranforms->device_address;
        pc.visible_addr = visible->device_address;
        pc.instance_count = ctx->frame->instance_count;

        cl.dd->command_bind_pipeline(cl.cmd, instance_cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Instance cull", (ctx->frame->instance_count + 63) / 64);
    };    
}

void ClusterCullFeature::_create_cluster_refs_args_pass()
{
    cluster_refs_args_pass.name = "ClusterRefsArgs";
    cluster_refs_args_pass.category = "ClusterCull";
    cluster_refs_args_pass.setup = [this](RenderGraph::Builder& b) {
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
        
        b.read_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterExpandArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cluster_refs_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto vis_inst = cl.graph->buffer("VisibleInstances");
        auto expand_args = cl.graph->buffer("ClusterExpandArgs");
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        
        struct Push {
            VkDeviceAddress visible_inst_addr;
            VkDeviceAddress expand_addr;
            VkDeviceAddress cluster_refs_addr;
        } pc;
        pc.visible_inst_addr = vis_inst->device_address;
        pc.expand_addr = expand_args->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_refs_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Cluster expand args", 1);
    };
}

void ClusterCullFeature::_create_cluster_refs_pass()
{
    cluster_refs_pass.name = "ClusterRefs";
    cluster_refs_pass.category = "ClusterCull";
    cluster_refs_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleInstances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterExpandArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    cluster_refs_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto visible = cl.graph->buffer("VisibleInstances");
        auto expand_args = cl.graph->buffer("ClusterExpandArgs");
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        
        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress visible_addr;
            VkDeviceAddress cluster_refs_addr;
        } pc;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.visible_addr = visible->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_refs_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster expand", *expand_args);
    };
}

void ClusterCullFeature::_create_cluster_cull_args_pass()
{
    cluster_cull_args_pass.name = "ClusterCullArgs";
    cluster_cull_args_pass.category = "ClusterCull";
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

        drivers::DeviceDriverVulkan::BufferCreateInfo counter_ci{};
        counter_ci.size = sizeof(uint32_t);
        counter_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        counter_ci.device_local = true;
        b.create_buffer("HiZCounter", counter_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo counts_ci{};
        counts_ci.size = (VkDeviceSize)(ctx->geometry->cluster_head ? ctx->geometry->cluster_head : 1u) * sizeof(uint32_t);
        counts_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        counts_ci.device_local = true;
        b.create_buffer("ClusterCounts", counts_ci);
        
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("HiZCounter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    cluster_cull_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto cluster_refs = cl.graph->buffer("ClusterRefs");
        auto cull_args = cl.graph->buffer("ClusterCullArgs");
        auto vis_clus = cl.graph->buffer("VisibleClusters");
        auto vis_clus2 = cl.graph->buffer("VisibleClusters2");
        auto retest = cl.graph->buffer("ClusterRetest");
        auto hiz_count = cl.graph->buffer("HiZCounter");
        auto counts = cl.graph->buffer("ClusterCounts");
        
        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress cull_addr;
            VkDeviceAddress vis_clus_addr;
            VkDeviceAddress vis_clus2_addr;
            VkDeviceAddress retest_addr;
            VkDeviceAddress hiz_count_addr;
        } pc;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.cull_addr = cull_args->device_address;
        pc.vis_clus_addr = vis_clus->device_address;
        pc.vis_clus2_addr = vis_clus2->device_address;
        pc.retest_addr = retest->device_address;
        pc.hiz_count_addr = hiz_count->device_address;

        cl.fill_buffer("Clear cluster counts", *counts, 0);

        cl.dd->command_bind_pipeline(cl.cmd, cluster_cull_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Cluster cull args", 1);
    };
}

void ClusterCullFeature::_create_cluster_cull_pass()
{
    cluster_cull_pass.name = "ClusterCull";
    cluster_cull_pass.category = "ClusterCull";
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
        
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress visible_clusters_addr;
            VkDeviceAddress retest_addr;
            uint32_t hiz_index;
            float px_per_unit;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.cluster_refs_addr = cluster_refs->device_address;
        pc.visible_clusters_addr = vis_clus->device_address;
        pc.retest_addr = retest->device_address;
        pc.hiz_index = hiz->bindless_sampled;
        pc.px_per_unit = ctx->frame->px_per_unit;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_cull_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster cull", *cull_args);
    };
}

void ClusterCullFeature::_create_raster_count_pass()
{
    raster_count_pass.name = "RasterCount1";
    raster_count_pass.category = "ClusterCull";
    raster_count_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterCullArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    raster_count_pass.execute = [this](RenderGraph::CommandList& cl) {
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

        cl.dd->command_bind_pipeline(cl.cmd, raster_count_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Raster count 1", *cull_args);
    };
}

void ClusterCullFeature::_create_raster_sum_pass()
{
    raster_sum_pass.name = "RasterSum1";
    raster_sum_pass.category = "ClusterCull";
    raster_sum_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo offsets_ci{};
        offsets_ci.size = (VkDeviceSize)(ctx->geometry->cluster_head ? ctx->geometry->cluster_head : 1u) * sizeof(uint32_t);
        offsets_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        offsets_ci.device_local = true;
        b.create_buffer("ClusterOffsets", offsets_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo cmds_ci{};
        cmds_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(VkDrawIndexedIndirectCommand);
        cmds_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        cmds_ci.device_local = true;
        b.create_buffer("ClusterDrawCmds", cmds_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo meta_ci{};
        meta_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(uint32_t);
        meta_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meta_ci.device_local = true;
        b.create_buffer("ClusterDrawMeta", meta_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo drawcount_ci{};
        drawcount_ci.size = sizeof(uint32_t);
        drawcount_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        drawcount_ci.device_local = true;
        b.create_buffer("RasterDrawCount", drawcount_ci);

        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ClusterCounts", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCmds", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawMeta", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("RasterDrawCount", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    raster_sum_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto counts = cl.graph->buffer("ClusterCounts");
        auto offsets = cl.graph->buffer("ClusterOffsets");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta");
        auto draw_count = cl.graph->buffer("RasterDrawCount");

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
        pc.cluster_count = ctx->geometry->cluster_head;

        cl.dd->command_bind_pipeline(cl.cmd, raster_sum_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Prefix sum", 1);
    };
}

void ClusterCullFeature::_create_raster_emit_pass()
{
    raster_emit_pass.name = "RasterEmit1";
    raster_emit_pass.category = "ClusterCull";
    raster_emit_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo scatter_ci{};
        scatter_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(uint32_t);
        scatter_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        scatter_ci.device_local = true;
        b.create_buffer("ClusterScatter", scatter_ci);

        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterOffsets", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterScatter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    raster_emit_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto refs = cl.graph->buffer("ClusterRefs");
        auto vis = cl.graph->buffer("VisibleClusters");
        auto offsets = cl.graph->buffer("ClusterOffsets");
        auto scatter = cl.graph->buffer("ClusterScatter");
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

        cl.dd->command_bind_pipeline(cl.cmd, raster_emit_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Raster emit 1", *retest_args);
    };
}

void ClusterCullFeature::_create_raster_visibility_pass()
{
    raster_visibility_pass.name = "ClusterRaster1";
    raster_visibility_pass.category = "ClusterCull";
    raster_visibility_pass.setup = [this](RenderGraph::Builder& b) {
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
        b.read_buffer("RasterDrawCount", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterScatter", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterDrawMeta", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    raster_visibility_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto scatter = cl.graph->buffer("ClusterScatter");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds");
        auto draw_count = cl.graph->buffer("RasterDrawCount");

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
        cl.dd->command_bind_pipeline(cl.cmd, raster_visibility_pipe);
        cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect_count("Cluster raster visibility", *draw_cmds, 0, *draw_count, 0, ctx->frame->cluster_ref_capacity, sizeof(VkDrawIndexedIndirectCommand));
    };
}

void ClusterCullFeature::_create_hiz_build_pass()
{
    hiz_build_pass.name = "HiZBuild1";
    hiz_build_pass.category = "ClusterCull";
    hiz_build_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("HiZ", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("HiZCounter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    hiz_build_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* hiz = cl.graph->image("HiZ");
        auto* depth = cl.graph->image("G_Depth");
        auto* counter = cl.graph->buffer("HiZCounter");

        uint32_t gx = (hiz->extent.width + 63) / 64;
        uint32_t gy = (hiz->extent.height + 63) / 64;

        struct {
            VkDeviceAddress counter;
            int32_t base_w, base_h;
            uint32_t depth_index, mips, num_workgroups, _pad;
            uint32_t mip_slot[16];
        } pc;
        pc.counter = counter->device_address;
        pc.base_w = (int32_t)depth->extent.width;
        pc.base_h = (int32_t)depth->extent.height;
        pc.depth_index = depth->bindless_sampled;
        pc.mips = hiz->mip_levels;
        pc.num_workgroups = gx * gy;
        for (uint32_t m = 0; m < hiz->mip_levels && m < 16 && m < hiz->mip_storage_slots.size(); m++) pc.mip_slot[m] = hiz->mip_storage_slots[m];

        cl.dd->command_bind_pipeline(cl.cmd, hiz_spd_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Hi-Z pyramid build", gx, gy);
    };
}

void ClusterCullFeature::_create_cluster_retest_args_pass()
{
    cluster_retest_args_pass.name = "ClusterRetestArgs";
    cluster_retest_args_pass.category = "ClusterCull";
    cluster_retest_args_pass.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo args_ci{};
        args_ci.size = sizeof(IndirectDispatch);
        args_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        args_ci.device_local = true;
        b.create_buffer("ClusterRetestArgs", args_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo counts_ci{};
        counts_ci.size = (VkDeviceSize)(ctx->geometry->cluster_head ? ctx->geometry->cluster_head : 1u) * sizeof(uint32_t);
        counts_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        counts_ci.device_local = true;
        b.create_buffer("ClusterCounts2", counts_ci);
        
        b.read_buffer("ClusterRetest", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("HiZCounter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    cluster_retest_args_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto retest = cl.graph->buffer("ClusterRetest");
        auto args = cl.graph->buffer("ClusterRetestArgs");
        auto hiz_count = cl.graph->buffer("HiZCounter");
        auto counts = cl.graph->buffer("ClusterCounts2");
        
        struct Push {
            VkDeviceAddress retest_addr;
            VkDeviceAddress args_addr;
            VkDeviceAddress hiz_count_addr;
        } pc;
        pc.retest_addr = retest->device_address;
        pc.args_addr = args->device_address;
        pc.hiz_count_addr = hiz_count->device_address;

        cl.fill_buffer("Clear cluster counts 2", *counts, 0);

        cl.dd->command_bind_pipeline(cl.cmd, cluster_retest_args_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Cluster retest args", 1);
    };
}

void ClusterCullFeature::_create_cluster_retest_pass()
{
    cluster_retest_pass.name = "ClusterRetest";
    cluster_retest_pass.category = "ClusterCull";
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
        
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress cluster_retest_addr;
            VkDeviceAddress visible_clusters_addr;
            uint32_t hiz_index;
            float px_per_unit;
        } pc;
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = inst->device_address;
        pc.transforms_addr = xf->device_address;
        pc.cluster_refs_addr = refs->device_address;
        pc.cluster_retest_addr = retest->device_address;
        pc.visible_clusters_addr = vis2->device_address;
        pc.hiz_index = hiz->bindless_sampled;
        pc.px_per_unit = 1.0f;

        cl.dd->command_bind_pipeline(cl.cmd, cluster_retest_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Cluster retest", *args);
    };
}

void ClusterCullFeature::_create_raster_count_2_pass()
{
    raster_count_pass_2.name = "RasterCount2";
    raster_count_pass_2.category = "ClusterCull";
    raster_count_pass_2.setup = [this](RenderGraph::Builder& b) {
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("VisibleClusters2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRetestArgs", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.write_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    raster_count_pass_2.execute = [this](RenderGraph::CommandList& cl) {
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

        cl.dd->command_bind_pipeline(cl.cmd, raster_count_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Raster count 1", *retest_args);
    };
}

void ClusterCullFeature::_create_raster_sum_2_pass()
{
    raster_sum_pass_2.name = "RasterSum2";
    raster_sum_pass_2.category = "ClusterCull";
    raster_sum_pass_2.setup = [this](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::BufferCreateInfo offsets_ci{};
        offsets_ci.size = (VkDeviceSize)(ctx->geometry->cluster_head ? ctx->geometry->cluster_head : 1u) * sizeof(uint32_t);
        offsets_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        offsets_ci.device_local = true;
        b.create_buffer("ClusterOffsets2", offsets_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo cmds_ci{};
        cmds_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(VkDrawIndexedIndirectCommand);
        cmds_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        cmds_ci.device_local = true;
        b.create_buffer("ClusterDrawCmds2", cmds_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo meta_ci{};
        meta_ci.size = (VkDeviceSize)ctx->frame->cluster_ref_capacity * sizeof(uint32_t);
        meta_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meta_ci.device_local = true;
        b.create_buffer("ClusterDrawMeta2", meta_ci);

        drivers::DeviceDriverVulkan::BufferCreateInfo drawcount_ci{};
        drawcount_ci.size = sizeof(uint32_t);
        drawcount_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        drawcount_ci.device_local = true;
        b.create_buffer("RasterDrawCount2", drawcount_ci);
    
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("ClusterCounts2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.write_buffer("ClusterOffsets2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawCmds2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("ClusterDrawMeta2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("RasterDrawCount2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    
    };
    raster_sum_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto geometry = cl.graph->buffer("Geometry");
        auto counts = cl.graph->buffer("ClusterCounts2");
        auto offsets = cl.graph->buffer("ClusterOffsets2");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds2");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta2");
        auto draw_count = cl.graph->buffer("RasterDrawCount2");

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
        pc.cluster_count = ctx->geometry->cluster_head;

        cl.dd->command_bind_pipeline(cl.cmd, raster_sum_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Prefix sum 2", 1);
    };
}

void ClusterCullFeature::_create_raster_emit_2_pass()
{
    raster_emit_pass_2.name = "RasterEmit2";
    raster_emit_pass_2.category = "ClusterCull";
    raster_emit_pass_2.setup = [this](RenderGraph::Builder& b) {
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
    raster_emit_pass_2.execute = [this](RenderGraph::CommandList& cl) {
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

        cl.dd->command_bind_pipeline(cl.cmd, raster_emit_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch_indirect("Raster emit 2", *retest_args);
    };
}

void ClusterCullFeature::_create_raster_visibility_2_pass()
{
    raster_visibility_pass_2.name = "ClusterRaster2";
    raster_visibility_pass_2.category = "ClusterCull";
    raster_visibility_pass_2.setup = [this](RenderGraph::Builder& b) {
        b.color_attachment("G_Visibility", VK_ATTACHMENT_LOAD_OP_LOAD);
        b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_LOAD);
        b.read_buffer("ClusterDrawCmds2", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("RasterDrawCount2", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterScatter2", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("ClusterDrawMeta2", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    raster_visibility_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto vis = cl.graph->image("G_Visibility");
        auto camera = cl.graph->buffer("Camera");
        auto geometry = cl.graph->buffer("Geometry");
        auto inst = cl.graph->buffer("Instances");
        auto xf = cl.graph->buffer("Transforms");
        auto refs = cl.graph->buffer("ClusterRefs");
        auto scatter = cl.graph->buffer("ClusterScatter2");
        auto draw_meta = cl.graph->buffer("ClusterDrawMeta2");
        auto draw_cmds = cl.graph->buffer("ClusterDrawCmds2");
        auto draw_count = cl.graph->buffer("RasterDrawCount2");

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
        cl.dd->command_bind_pipeline(cl.cmd, raster_visibility_pipe);
        cl.dd->command_bind_index_buffer(cl.cmd, ctx->geometry->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect_count("Cluster raster visibility 2", *draw_cmds, 0, *draw_count, 0, ctx->frame->cluster_ref_capacity, sizeof(VkDrawIndexedIndirectCommand));
    };
}

void ClusterCullFeature::_create_hiz_build_2_pass()
{
    hiz_build_pass_2.name = "HiZBuild2";
    hiz_build_pass_2.category = "ClusterCull";
    hiz_build_pass_2.setup = [this](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("HiZ", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("HiZCounter", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    hiz_build_pass_2.execute = [this](RenderGraph::CommandList& cl) {
        auto* hiz = cl.graph->image("HiZ");
        auto* depth = cl.graph->image("G_Depth");
        auto* counter = cl.graph->buffer("HiZCounter");

        uint32_t gx = (hiz->extent.width + 63) / 64;
        uint32_t gy = (hiz->extent.height + 63) / 64;

        struct {
            VkDeviceAddress counter;
            int32_t base_w, base_h;
            uint32_t depth_index, mips, num_workgroups, _pad;
            uint32_t mip_slot[16];
        } pc;
        pc.counter = counter->device_address;
        pc.base_w = (int32_t)depth->extent.width;
        pc.base_h = (int32_t)depth->extent.height;
        pc.depth_index = depth->bindless_sampled;
        pc.mips = hiz->mip_levels;
        pc.num_workgroups = gx * gy;
        for (uint32_t m = 0; m < hiz->mip_levels && m < 16 && m < hiz->mip_storage_slots.size(); m++) pc.mip_slot[m] = hiz->mip_storage_slots[m];

        cl.dd->command_bind_pipeline(cl.cmd, hiz_spd_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Hi-Z pyramid build", gx, gy);
    };
}

void ClusterCullFeature::_create_material_resolve_pass()
{
    material_resolve_pass.name = "MaterialResolve";
    material_resolve_pass.category = "Material";
    material_resolve_pass.never_cull = true;
    material_resolve_pass.setup = [this](RenderGraph::Builder& b) {
        
        drivers::DeviceDriverVulkan::ImageCreateInfo normal_ci{};
        normal_ci.name = "G_Normal";
        normal_ci.format = VK_FORMAT_R16G16_UNORM;
        normal_ci.usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("G_Normal", normal_ci);

        drivers::DeviceDriverVulkan::ImageCreateInfo albedo_ci{};
        albedo_ci.name = "G_Albedo";
        albedo_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
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

Error ClusterCullFeature::create_resources()
{
    _create_clear_visible_pass();
    _create_instance_cull_pass();
    _create_cluster_refs_args_pass();
    _create_cluster_refs_pass();
    _create_cluster_cull_args_pass();
    _create_cluster_cull_pass();
    _create_raster_count_pass();
    _create_raster_sum_pass();
    _create_raster_emit_pass();
    _create_raster_visibility_pass();
    _create_hiz_build_pass();
    _create_cluster_retest_args_pass();
    _create_cluster_retest_pass();
    _create_raster_count_2_pass();
    _create_raster_sum_2_pass();
    _create_raster_emit_2_pass();
    _create_raster_visibility_2_pass();
    _create_hiz_build_2_pass();
    _create_material_resolve_pass();
    return Error::Ok;
};

Error ClusterCullFeature::create_pipelines()
{
    using enum Error;

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_INSTANCE_CULL_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/instance_cull.comp" });
    instance_cull_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/instance_cull"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_REFS_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_refs_args.comp" });
    cluster_refs_args_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_refs_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_REFS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_refs.comp" });
    cluster_refs_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_refs"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_CULL_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_cull_args.comp" });
    cluster_cull_args_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_cull_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_CULL_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_cull.comp" });
    cluster_cull_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_cull"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_RASTER_COUNT_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/raster_count.comp" });
    raster_count_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/raster_count"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_RASTER_SUM_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/raster_sum.comp" });
    raster_sum_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/raster_sum"});
    ctx->dd->shader_free(cs);
    }

    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_RASTER_EMIT_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/raster_emit.comp" });
    raster_emit_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/raster_emit"});
    ctx->dd->shader_free(cs);
    }

    {
    VkRenderPass rp = ctx->graph->acquire_render_pass(raster_visibility_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_RASTER_VISIBILITY_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_RASTER_VISIBILITY_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Vertex,   .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "raster_visibility_vs" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Fragment, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "raster_visibility_fs" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.vertex_shader = vs; pipeline_ci.fragment_shader = fs; pipeline_ci.render_pass = rp;
    pipeline_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // pipeline_ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    pipeline_ci.cull_mode = VK_CULL_MODE_NONE;
    pipeline_ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_ci.depth_test = true;
    pipeline_ci.depth_write = true;
    pipeline_ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    pipeline_ci.name = "raster_visibility_pipeline";
    raster_visibility_pipe = ctx->dd->graphics_pipeline_create(pipeline_ci);
    ctx->dd->shader_free(vs); ctx->dd->shader_free(fs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_HIZ_SPD_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/hiz_spd.comp" });
    hiz_spd_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/hiz_spd"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_RETEST_ARGS_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_retest_args.comp" });
    cluster_retest_args_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_retest_args"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_CLUSTER_RETEST_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/cluster_retest.comp" });
    cluster_retest_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/cluster_retest"});
    ctx->dd->shader_free(cs);
    }
    
    {
    EmbeddedResource::Blob comp_blob = EmbeddedResource::load(L"SHADERS_CLUSTER_CULL_MATERIAL_RESOLVE_COMP");
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::Compute, .glsl = (const char*)comp_blob.data, .glsl_size = comp_blob.size, .name = "cluster_cull/material_resolve.comp" });
    material_resolve_pipe = ctx->dd->compute_pipeline_create({cs, "cluster_cull/material_resolve"});
    ctx->dd->shader_free(cs);
    }

    return Ok;
}

void ClusterCullFeature::destroy_resources()
{
    ctx->dd->pipeline_free(instance_cull_pipe);
    ctx->dd->pipeline_free(cluster_refs_args_pipe);
    ctx->dd->pipeline_free(cluster_refs_pipe);
    ctx->dd->pipeline_free(cluster_cull_args_pipe);
    ctx->dd->pipeline_free(cluster_cull_pipe);
    ctx->dd->pipeline_free(raster_count_pipe);
    ctx->dd->pipeline_free(raster_sum_pipe);
    ctx->dd->pipeline_free(raster_emit_pipe);
    ctx->dd->pipeline_free(raster_visibility_pipe);
    ctx->dd->pipeline_free(hiz_spd_pipe);
    ctx->dd->pipeline_free(cluster_retest_args_pipe);
    ctx->dd->pipeline_free(cluster_retest_pipe);
    ctx->dd->pipeline_free(material_resolve_pipe);
}

void ClusterCullFeature::build(RenderGraph& g)
{
    if (!enabled || !ctx->geometry->allocated) return;
    g.add(&clear_visible_pass);
    g.add(&instance_cull_pass);
    g.add(&cluster_refs_args_pass);
    g.add(&cluster_refs_pass);
    g.add(&cluster_cull_args_pass);
    g.add(&cluster_cull_pass);
    g.add(&raster_count_pass);
    g.add(&raster_sum_pass);
    g.add(&raster_emit_pass);
    g.add(&raster_visibility_pass);
    g.add(&hiz_build_pass);
    g.add(&cluster_retest_args_pass);
    g.add(&cluster_retest_pass);
    g.add(&raster_count_pass_2);
    g.add(&raster_sum_pass_2);
    g.add(&raster_emit_pass_2);
    g.add(&raster_visibility_pass_2);
    g.add(&hiz_build_pass_2);
    g.add(&material_resolve_pass);
};

}