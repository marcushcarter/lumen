#include <core/rendering/features/editor/outline_feature.h>
#include <core/rendering/features/editor/editor_overlay.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/io/embedded_resource.h>
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstring>

namespace lumen {

void OutlineFeature::set_selection(const Entity* p_entities, uint32_t p_count)
{
    uint32_t max_index = 0;
    for (uint32_t i = 0; i < p_count; i++) max_index = std::max(max_index, p_entities[i].index);
    words.assign(p_count ? max_index / 32 + 1 : 0, 0u);
    for (uint32_t i = 0; i < p_count; i++) words[p_entities[i].index >> 5] |= 1u << (p_entities[i].index & 31);
    selected_count = p_count;
    version++;
}

bool OutlineFeature::_selected(uint32_t p_entity) const
{
    const uint32_t w = p_entity >> 5;
    return w < words.size() && ((words[w] >> (p_entity & 31)) & 1u) != 0u;
}

void OutlineFeature::_gather_mask(MaskSlot& r_slot)
{
    mask_count = 0;
    mask_max_nodes = 0;
    const FrameData& f = *ctx->frame;
    MaskInstance* out = reinterpret_cast<MaskInstance*>(static_cast<uint8_t*>(r_slot.instances.mapped) + MASK_INSTANCES_HEADER);
    const glm::vec3 cam = glm::vec3(f.camera.position);
    const float ppu = f.px_per_unit * MASK_SCALE;
    const auto add = [&](const Instance& p_instance, uint32_t p_index, const glm::mat4& p_model) {
        if (mask_count >= MASK_MAX_INSTANCES) return;
        const LMesh* mesh = ctx->geometry->get(p_instance.mesh_id);
        if (!mesh) return;
        const float scale = std::max(glm::length(glm::vec3(p_model[0])), std::max(glm::length(glm::vec3(p_model[1])), glm::length(glm::vec3(p_model[2]))));
        const glm::vec3 center = glm::vec3(p_model * glm::vec4(mesh->pos_min + mesh->pos_extent * 0.5f, 1.0f));
        const float radius = glm::length(mesh->pos_extent) * 0.5f * scale;
        const float distance = std::max(glm::length(center - cam) - radius, MASK_MIN_DISTANCE);
        const float density = scale * ppu / distance;
        if (!std::isfinite(density) || density <= 0.0f) return;
        out[mask_count++] = { p_index, p_instance.mesh_id, p_instance.slot_base, density };
        mask_max_nodes = std::max(mask_max_nodes, mesh->node_count);
    };
    const StaticInstances& st = f.static_insts;
    for (uint32_t i = 0; i < (uint32_t)st.instances.size(); i++) {
        if (_selected(st.instances[i].entity_id)) add(st.instances[i], i, st.transforms[i].curr_mtx);
    }
    const uint32_t base = (uint32_t)st.instances.size();
    for (uint32_t j = 0; j < (uint32_t)f.instances_scratch.size(); j++) {
        if (_selected(f.instances_scratch[j].entity_id)) add(f.instances_scratch[j], base + j, f.transforms_scratch[j].curr_mtx);
    }
    const uint32_t header[4] = { mask_count, 0u, 0u, 0u };
    std::memcpy(r_slot.instances.mapped, header, sizeof(header));
    ctx->dd->buffer_flush(r_slot.instances);
}

void OutlineFeature::_create_mask_passes()
{
    mask_reset_pass.name = "SelectionMaskReset";
    mask_reset_pass.category = PASS_CATEGORY_EDITOR;
    mask_reset_pass.setup = [](RenderGraph::Builder& b) {
        b.write_buffer("SelectionMaskList", VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    };
    mask_reset_pass.execute = [](RenderGraph::CommandList& cl) {
        auto* list = cl.graph->buffer("SelectionMaskList");
        if (!list) return;
        const uint32_t header[MASK_LIST_HEADER / 4] = { 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u };
        cl.update_buffer("Selection mask reset", *list, header, sizeof(header));
    };

    mask_select_pass.name = "SelectionMaskSelect";
    mask_select_pass.category = PASS_CATEGORY_EDITOR;
    mask_select_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.write_buffer("SelectionMaskList", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("SelectionMaskIndices", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        b.write_buffer("SelectionMaskMaterials", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    mask_select_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* geometry = cl.graph->buffer("Geometry");
        auto* list = cl.graph->buffer("SelectionMaskList");
        auto* indices = cl.graph->buffer("SelectionMaskIndices");
        auto* materials = cl.graph->buffer("SelectionMaskMaterials");
        if (!geometry || !list || !indices || !materials || mask_count == 0) return;
        struct Push {
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress indices_addr;
            VkDeviceAddress materials_addr;
            uint32_t cluster_capacity;
            uint32_t index_capacity;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = mask_slots[cl.graph->current_frame % mask_slots.size()].instances.device_address;
        pc.list_addr = list->device_address;
        pc.indices_addr = indices->device_address;
        pc.materials_addr = materials->device_address;
        pc.cluster_capacity = MASK_MAX_CLUSTERS;
        pc.index_capacity = MASK_MAX_INDICES;
        cl.dd->command_bind_pipeline(cl.cmd, mask_select_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Selection mask select", std::max(1u, (mask_max_nodes + MASK_GROUP_SIZE - 1) / MASK_GROUP_SIZE), mask_count);
    };

    mask_draw_pass.name = "SelectionMask";
    mask_draw_pass.category = PASS_CATEGORY_EDITOR;
    mask_draw_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "SelectionMask";
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        ci.width_scale = MASK_SCALE;
        ci.height_scale = MASK_SCALE;
        b.create_image("SelectionMask", ci);
        b.depth_attachment("SelectionMask", VK_ATTACHMENT_LOAD_OP_CLEAR, [] { VkClearValue v{}; v.depthStencil = { 0.0f, 0 }; return v; }());
        b.read_buffer("SelectionMaskList", VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SelectionMaskIndices", VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
        b.read_buffer("SelectionMaskMaterials", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Geometry", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Transforms", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    };
    mask_draw_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* mask = cl.graph->image("SelectionMask");
        auto* list = cl.graph->buffer("SelectionMaskList");
        auto* indices = cl.graph->buffer("SelectionMaskIndices");
        auto* materials = cl.graph->buffer("SelectionMaskMaterials");
        auto* camera = cl.graph->buffer("Camera");
        auto* geometry = cl.graph->buffer("Geometry");
        auto* instances = cl.graph->buffer("Instances");
        auto* transforms = cl.graph->buffer("Transforms");
        if (!mask || !list || !indices || !materials || !camera || !geometry || !instances || !transforms || mask_count == 0) return;
        struct Push {
            VkDeviceAddress camera_addr;
            VkDeviceAddress geometry_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress transforms_addr;
            VkDeviceAddress list_addr;
            VkDeviceAddress materials_addr;
            uint32_t sampler_index;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);
        pc.camera_addr = camera->device_address;
        pc.geometry_addr = geometry->device_address;
        pc.instances_addr = instances->device_address;
        pc.transforms_addr = transforms->device_address;
        pc.list_addr = list->device_address;
        pc.materials_addr = materials->device_address;
        pc.sampler_index = mask_sampler.bindless_sampler;
        cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, mask->extent }});
        cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, mask->extent }});
        cl.dd->command_bind_index_buffer(cl.cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
        cl.dd->command_bind_pipeline(cl.cmd, mask_draw_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.draw_indexed_indirect("Selection mask", *list, 0, 1, MASK_DRAW_STRIDE);
    };
}

void OutlineFeature::_create_outline_pass()
{
    outline_pass.name = "SelectionOutline";
    outline_pass.category = PASS_CATEGORY_EDITOR;
    outline_pass.setup = [this](RenderGraph::Builder& b) {
        b.read_image("G_Visibility", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_buffer("ClusterRefs", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("Instances", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        b.read_buffer("SelectionWords", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (pass_mask) b.read_image("SelectionMask", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image(editor_overlay_target(*b.graph), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    outline_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* vis = cl.graph->image("G_Visibility");
        auto* depth = cl.graph->image("G_Depth");
        auto* refs = cl.graph->buffer("ClusterRefs");
        auto* instances = cl.graph->buffer("Instances");
        auto* mask = cl.graph->buffer("SelectionWords");
        auto* out = cl.graph->image(editor_overlay_target(*cl.graph));
        if (!vis || !depth || !refs || !instances || !mask || !out) return;

        struct Push {
            VkDeviceAddress cluster_refs_addr;
            VkDeviceAddress instances_addr;
            VkDeviceAddress mask_addr;
            float color[4];
            uint32_t vis_index;
            uint32_t depth_index;
            uint32_t out_slot;
            int32_t radius;
            uint32_t rect[4];
            uint32_t size[2];
            uint32_t vis_size[2];
            uint32_t hover;
            uint32_t hover_pixel[2];
            uint32_t terrain;
            uint32_t mask_index;
            uint32_t mask_size[2];
            float hidden_alpha;
        } pc{};
        static_assert(sizeof(Push) <= drivers::DeviceDriverVulkan::BindlessHeap::PUSH_CONSTANT_SIZE);

        const VkExtent2D extent = out->extent;
        const float pad = (float)std::clamp(radius, 1, 8) + 1.0f;
        const float x0 = std::floor(pass_rect_min.x * (float)extent.width - pad);
        const float y0 = std::floor(pass_rect_min.y * (float)extent.height - pad);
        const float x1 = std::ceil(pass_rect_max.x * (float)extent.width + pad);
        const float y1 = std::ceil(pass_rect_max.y * (float)extent.height + pad);
        pc.rect[0] = (uint32_t)std::clamp(x0, 0.0f, (float)extent.width);
        pc.rect[1] = (uint32_t)std::clamp(y0, 0.0f, (float)extent.height);
        pc.rect[2] = (uint32_t)std::clamp(x1, 0.0f, (float)extent.width);
        pc.rect[3] = (uint32_t)std::clamp(y1, 0.0f, (float)extent.height);
        if (!(pc.rect[2] > pc.rect[0] && pc.rect[3] > pc.rect[1])) return;
        pc.cluster_refs_addr = refs->device_address;
        pc.instances_addr = instances->device_address;
        pc.mask_addr = mask->device_address;
        const glm::vec4 c = pass_hover ? hover_color : color;
        pc.color[0] = c.r;
        pc.color[1] = c.g;
        pc.color[2] = c.b;
        pc.color[3] = c.a;
        pc.hover = pass_hover ? 1u : 0u;
        pc.terrain = terrain && !pass_hover ? 1u : 0u;
        auto* mask_image = pass_mask ? cl.graph->image("SelectionMask") : nullptr;
        pc.mask_index = mask_image ? mask_image->bindless_sampled : 0xFFFFFFFFu;
        pc.mask_size[0] = mask_image ? mask_image->extent.width : 0u;
        pc.mask_size[1] = mask_image ? mask_image->extent.height : 0u;
        pc.hidden_alpha = hidden_alpha;
        pc.hover_pixel[0] = std::min((uint32_t)(std::clamp(pass_hover_uv.x, 0.0f, 1.0f) * (float)vis->extent.width), vis->extent.width - 1);
        pc.hover_pixel[1] = std::min((uint32_t)(std::clamp(pass_hover_uv.y, 0.0f, 1.0f) * (float)vis->extent.height), vis->extent.height - 1);
        pc.vis_index = vis->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.radius = std::clamp(radius, 1, 8);
        pc.size[0] = extent.width;
        pc.size[1] = extent.height;
        pc.vis_size[0] = vis->extent.width;
        pc.vis_size[1] = vis->extent.height;

        cl.dd->command_bind_pipeline(cl.cmd, outline_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Selection outline", (pc.rect[2] - pc.rect[0] + 7) / 8, (pc.rect[3] - pc.rect[1] + 7) / 8);
    };
}

Error OutlineFeature::create_resources()
{
    const uint32_t count = ctx->graph->frame_count;
    mask_buffers.resize(count);
    slot_version.assign(count, UINT64_MAX);
    mask_slots.resize(count);
    for (MaskSlot& s : mask_slots) {
        s.instances = ctx->dd->buffer_create({ .size = MASK_INSTANCES_HEADER + (VkDeviceSize)MASK_MAX_INSTANCES * sizeof(MaskInstance), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .name = "selection_mask_instances" });
        s.list = ctx->dd->buffer_create({ .size = MASK_LIST_HEADER + (VkDeviceSize)MASK_MAX_CLUSTERS * 2 * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = true, .name = "selection_mask_list" });
        s.indices = ctx->dd->buffer_create({ .size = (VkDeviceSize)MASK_MAX_INDICES * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .device_local = true, .name = "selection_mask_indices" });
        s.materials = ctx->dd->buffer_create({ .size = (VkDeviceSize)MASK_MAX_TRIANGLES * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = true, .name = "selection_mask_materials" });
        LUMEN_ERR_FAIL_COND_V(!s.instances.buffer || !s.list.buffer || !s.indices.buffer || !s.materials.buffer, Error::FAILED);
    }
    drivers::DeviceDriverVulkan::SamplerCreateInfo sampler_ci{};
    sampler_ci.name = "selection_mask_material";
    mask_sampler = ctx->dd->sampler_create(sampler_ci);
    LUMEN_ERR_FAIL_COND_V(!mask_sampler.sampler, Error::FAILED);
    _create_outline_pass();
    _create_mask_passes();
    return Error::OK;
}

Error OutlineFeature::create_pipelines()
{
    outline_pipe = _compute_pipeline(L"SHADERS_EDITOR_OUTLINE_COMP", "editor/outline.comp");
    mask_select_pipe = _compute_pipeline(L"SHADERS_EDITOR_OUTLINE_SELECT_COMP", "editor/outline_select.comp");

    using Stage = drivers::DeviceDriverVulkan::ShaderStage;
    auto shader = [&](const wchar_t* p_resource, Stage p_stage, const char* p_name) {
        EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
        return ctx->dd->shader_create({ .stage = p_stage, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    };
    VkShaderModule vs = shader(L"SHADERS_EDITOR_OUTLINE_MASK_VERT", Stage::VERTEX, "editor/outline_mask.vert");
    VkShaderModule fs = shader(L"SHADERS_EDITOR_OUTLINE_MASK_FRAG", Stage::FRAGMENT, "editor/outline_mask.frag");
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.render_pass = ctx->graph->acquire_render_pass(mask_draw_pass);
    ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.cull_mode = VK_CULL_MODE_NONE;
    ci.depth_test = true;
    ci.depth_write = true;
    ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.name = "editor/outline_mask";
    mask_draw_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(fs);
    return Error::OK;
}

void OutlineFeature::destroy_resources()
{
    ctx->dd->pipeline_free(outline_pipe);
    ctx->dd->pipeline_free(mask_select_pipe);
    ctx->dd->pipeline_free(mask_draw_pipe);
    for (MaskSlot& s : mask_slots) {
        if (s.instances.buffer) ctx->dd->buffer_free(s.instances);
        if (s.list.buffer) ctx->dd->buffer_free(s.list);
        if (s.indices.buffer) ctx->dd->buffer_free(s.indices);
        if (s.materials.buffer) ctx->dd->buffer_free(s.materials);
    }
    mask_slots.clear();
    if (mask_sampler.sampler) ctx->dd->sampler_free(mask_sampler);
    mask_sampler = {};
    for (drivers::DeviceDriverVulkan::Buffer& b : mask_buffers) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    mask_buffers.clear();
    slot_version.clear();
}

void OutlineFeature::build(RenderGraph& g)
{
    if (mask_buffers.empty()) return;
    const bool any_selected = selected_count != 0 || terrain;
    const bool hovering = !any_selected && hover;
    if (!hovering && (!any_selected || rect_max.x <= rect_min.x || rect_max.y <= rect_min.y)) return;
    if (!editor_overlay_target(g) || !g.image_resource("G_Visibility") || !g.image_resource("G_Depth") || !g.buffer_resource("ClusterRefs") || !g.buffer_resource("Instances")) return;

    const uint32_t slot = g.current_frame;
    drivers::DeviceDriverVulkan::Buffer& buf = mask_buffers[slot];
    const VkDeviceSize bytes = (words.size() + 1) * sizeof(uint32_t);
    if (!buf.buffer || buf.size < bytes) {
        if (buf.buffer) ctx->dd->buffer_free(buf);
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = std::max<VkDeviceSize>(MIN_MASK_BYTES, std::bit_ceil(bytes));
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.name = "SelectionWords";
        buf = ctx->dd->buffer_create(ci);
        LUMEN_ERR_FAIL_COND(!buf.buffer);
        slot_version[slot] = UINT64_MAX;
    }
    if (slot_version[slot] != version) {
        const uint32_t count = (uint32_t)words.size();
        std::memcpy(buf.mapped, &count, sizeof(count));
        std::memcpy((uint8_t*)buf.mapped + sizeof(count), words.data(), words.size() * sizeof(uint32_t));
        ctx->dd->buffer_flush(buf);
        slot_version[slot] = version;
    }
    pass_mask = false;
    if (!hovering && selected_count != 0 && !terrain && !mask_slots.empty() && ctx->frame && ctx->geometry) {
        MaskSlot& ms = mask_slots[slot % mask_slots.size()];
        _gather_mask(ms);
        if (mask_count != 0) {
            pass_mask = true;
            g.import_buffer("SelectionMaskList", &ms.list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer("SelectionMaskIndices", &ms.indices, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.import_buffer("SelectionMaskMaterials", &ms.materials, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
            g.add(&mask_reset_pass);
            g.add(&mask_select_pass);
            g.add(&mask_draw_pass);
        }
    }
    pass_hover = hovering;
    pass_hover_uv = hover_uv;
    pass_rect_min = hovering ? glm::vec2(0.0f) : rect_min;
    pass_rect_max = hovering ? glm::vec2(1.0f) : rect_max;

    g.import_buffer("SelectionWords", &buf, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    g.add(&outline_pass);
}

}
