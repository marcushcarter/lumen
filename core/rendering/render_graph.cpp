#include <core/rendering/render_graph.h>
#include <unordered_set>

namespace lumen {

/***************/
/**** SETUP ****/
/***************/

Error RenderGraph::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;

    dd = &r_dd;
    frame_count = p_frame_count;
    current_frame = 0;
    image_transient_pools.clear();
    image_transient_pools.resize(frame_count);
    buffer_transient_pools.clear();
    buffer_transient_pools.resize(frame_count);

    profiler.initialize(r_dd, p_frame_count);

    return Ok;
}

void RenderGraph::shutdown()
{
    dd->device_wait_idle();

    profiler.shutdown();

    for (auto& [k, fb] : framebuffer_cache) dd->framebuffer_free(fb);
    framebuffer_cache.clear();
    for (auto& [k, rp] : render_pass_cache) dd->render_pass_free(rp);
    render_pass_cache.clear();

    for (ImageTransientPool& pool : image_transient_pools) {
        for (auto& [key, imgs] : pool.free)
            for (drivers::DeviceDriverVulkan::Image& img : imgs) dd->image_free(img);
        pool.free.clear();
    }
    image_transient_pools.clear();

    for (BufferTransientPool& pool : buffer_transient_pools) {
        for (auto& [key, bufs] : pool.free)
            for (drivers::DeviceDriverVulkan::Buffer& buf : bufs) dd->buffer_free(buf);
        pool.free.clear();
    }
    buffer_transient_pools.clear();
}

Error RenderGraph::set_size(uint32_t p_width, uint32_t p_height)
{
    using enum Error;

    if (p_width == 0 || p_height == 0) return Ok;
    if (p_width == width && p_height == height) return Ok;
    width = p_width;
    height = p_height;

    for (auto& [k, fb] : framebuffer_cache) dd->framebuffer_free(fb);
    framebuffer_cache.clear();

    for (ImageTransientPool& pool : image_transient_pools) {
        for (auto& [key, imgs] : pool.free)
            for (drivers::DeviceDriverVulkan::Image& img : imgs) dd->image_free(img);
        pool.free.clear();
    }

    for (BufferTransientPool& pool : buffer_transient_pools) {
        for (auto& [key, bufs] : pool.free)
            for (drivers::DeviceDriverVulkan::Buffer& buf : bufs) dd->buffer_free(buf);
        pool.free.clear();
    }

    return Ok;
}

/***************/
/**** NAMES ****/
/***************/

uint64_t RenderGraph::intern(std::string_view p_name)
{
    uint64_t hash = 1469598103934665603ull;
    for (char c : p_name) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t RenderGraph::intern_named(std::string_view p_name)
{
    uint64_t id = intern(p_name);
    if (!debug_names.contains(id)) debug_names.emplace(id, std::string(p_name));
    return id;
}

/*******************/
/**** RESOURCES ****/
/*******************/

void RenderGraph::release_transients()
{
    _image_release_transients();
    _buffer_release_transients();
}

// ----- IMAGE -----

drivers::DeviceDriverVulkan::Image* RenderGraph::image(std::string_view p_name)
{
    auto it = image_resource_map.find(intern(p_name));
    if (it == image_resource_map.end()) return nullptr;
    return image_resources[it->second].image;
}

RenderGraph::ImageResource* RenderGraph::image_resource(std::string_view p_name)
{
    auto it = image_resource_map.find(intern(p_name));
    if (it == image_resource_map.end()) return nullptr;
    return &image_resources[it->second];
}

RenderGraph::ImageResource* RenderGraph::image_resource_by_id(uint64_t p_name_id)
{
    auto it = image_resource_map.find(p_name_id);
    if (it == image_resource_map.end()) return nullptr;
    return &image_resources[it->second];
}

void RenderGraph::import_image(std::string_view p_name, drivers::DeviceDriverVulkan::Image* p_image, VkImageLayout p_final_layout, VkPipelineStageFlags2 p_final_stage, VkAccessFlags2 p_final_access)
{
    uint64_t id = intern_named(p_name);

    ImageResource r{};
    r.kind = ResourceKind::Imported;
    r.name_id = id;
    r.image = p_image; 
    r.final_layout = p_final_layout;
    r.final_stage = p_final_stage;
    r.final_access = p_final_access;

    p_image->state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    p_image->state.stage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    p_image->state.access = 0;

    declared_image_formats[id] = p_image->format;

    uint32_t res_idx = static_cast<uint32_t>(image_resources.size());
    image_resources.push_back(r);
    auto [it, inserted] = image_resource_map.insert({ id, res_idx });
}

void RenderGraph::create_image(std::string_view p_name, const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info)
{
    uint64_t id = intern_named(p_name);
    if (image_resource_map.contains(id)) return;

    ImageResource r{};
    r.kind = ResourceKind::Transient;
    r.name_id = id;
    r.image = nullptr;
    r.image_create_info = p_create_info;
    r.image_create_info.pool = dd->image_transient_pool;
    r.final_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    declared_image_formats[id] = p_create_info.format;

    uint32_t idx = static_cast<uint32_t>(image_resources.size());
    image_resources.push_back(r);
    image_resource_map[id] = idx;    
}

uint64_t RenderGraph::_image_transient_key(const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info, VkExtent2D p_extent)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(p_create_info.format);
    mix(p_create_info.usage);
    mix(p_create_info.aspect);
    mix(p_create_info.samples);
    mix(p_create_info.mip_levels);
    mix(p_create_info.layers);
    mix(p_extent.width);
    mix(p_extent.height);
    return h;
}

void RenderGraph::_image_resolve_extent(const drivers::DeviceDriverVulkan::ImageCreateInfo& p_ci, uint32_t& r_w, uint32_t& r_h)
{
    if (p_ci.sizing == drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::Fixed) {
        r_w = p_ci.fixed_width;
        r_h = p_ci.fixed_height;
        return;
    }
    r_w = std::max(1u, static_cast<uint32_t>(std::lround(width * p_ci.width_scale)));
    r_h = std::max(1u, static_cast<uint32_t>(std::lround(height * p_ci.height_scale)));
}

void RenderGraph::_image_materialize_transient(ImageResource& r)
{
    if (r.image) return;
    
    drivers::DeviceDriverVulkan::ImageCreateInfo image_ci = r.image_create_info;
    image_ci.name = debug_names[r.name_id].c_str();

    uint32_t w, h;
    _image_resolve_extent(image_ci, w, h);
    VkExtent2D extent{ w, h };

    uint64_t key = _image_transient_key(image_ci, extent);
    ImageTransientPool& pool = image_transient_pools[current_frame];

    drivers::DeviceDriverVulkan::Image img;
    auto it = pool.free.find(key);
    if (it != pool.free.end() && !it->second.empty()) {
        img = it->second.back();
        it->second.pop_back();
        ++image_reuse_hits;
    } else {
        img = dd->image_create_dedicated(image_ci, extent);
        ++image_reuse_misses;
    }

    img.state = {};

    r.transient_storage = img;
    r.image = &r.transient_storage;
}

void RenderGraph::_image_release_transients()
{
    if (image_transient_pools.empty()) return;
    ImageTransientPool& pool = image_transient_pools[current_frame];
    std::unordered_set<VkImage> returned;
    for (ImageResource& r : image_resources) {
        if (r.kind != ResourceKind::Transient) continue;
        if (!r.image) continue;

        VkImage vk = r.transient_storage.image;
        if (vk != VK_NULL_HANDLE && returned.insert(vk).second) {
            uint64_t key = _image_transient_key(r.image_create_info, r.transient_storage.extent);
            pool.free[key].push_back(r.transient_storage);
        }
        r.image = nullptr;
        r.transient_storage = {};
    }
}

void RenderGraph::declare_image_format(std::string_view p_name, VkFormat p_format)
{
    declared_image_formats[intern_named(p_name)] = p_format;
}

// ----- BUFFER -----

drivers::DeviceDriverVulkan::Buffer* RenderGraph::buffer(std::string_view p_name)
{
    auto it = buffer_resource_map.find(intern(p_name));
    if (it == buffer_resource_map.end()) return nullptr;
    return buffer_resources[it->second].buffer;
}

RenderGraph::BufferResource* RenderGraph::buffer_resource(std::string_view p_name)
{
    auto it = buffer_resource_map.find(intern(p_name));
    if (it == buffer_resource_map.end()) return nullptr;
    return &buffer_resources[it->second];
}

RenderGraph::BufferResource* RenderGraph::buffer_resource_by_id(uint64_t p_name_id)
{
    auto it = buffer_resource_map.find(p_name_id);
    if (it == buffer_resource_map.end()) return nullptr;
    return &buffer_resources[it->second];
}

void RenderGraph::import_buffer(std::string_view p_name, drivers::DeviceDriverVulkan::Buffer* p_buffer, VkPipelineStageFlags2 p_final_stage, VkAccessFlags2 p_final_access)
{
    uint64_t id = intern_named(p_name);

    BufferResource r{};
    r.kind = ResourceKind::Imported;
    r.name_id = id;
    r.buffer = p_buffer;
    r.final_stage = p_final_stage;
    r.final_access = p_final_access;

    p_buffer->state.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    p_buffer->state.access = 0;

    uint32_t res_idx = static_cast<uint32_t>(buffer_resources.size());
    buffer_resources.push_back(r);
    buffer_resource_map.insert({ id, res_idx });
}

void RenderGraph::create_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::BufferCreateInfo& p_create_info)
{
    uint64_t id = intern_named(p_name);
    if (buffer_resource_map.contains(id)) return;

    BufferResource r{};
    r.kind = ResourceKind::Transient;
    r.name_id = id;
    r.buffer = nullptr;
    r.buffer_create_info = p_create_info;
    r.buffer_create_info.pool = dd->buffer_device_pool;

    uint32_t idx = static_cast<uint32_t>(buffer_resources.size());
    buffer_resources.push_back(r);
    buffer_resource_map[id] = idx;
}

uint64_t RenderGraph::_buffer_transient_key(VkBufferUsageFlags p_usage, bool p_device_local, bool p_host_visible, bool p_cpu_read, VkDeviceSize p_capacity)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix((uint64_t)p_usage);
    mix((uint64_t)p_device_local | ((uint64_t)p_host_visible << 1) | ((uint64_t)p_cpu_read << 2));
    mix((uint64_t)p_capacity);
    return h;
}

void RenderGraph::_buffer_materialize_transient(BufferResource& r)
{
    if (r.buffer) return;

    drivers::DeviceDriverVulkan::BufferCreateInfo buffer_ci = r.buffer_create_info;
    buffer_ci.name = debug_names[r.name_id].c_str();

    VkDeviceSize logical = buffer_ci.size ? buffer_ci.size : 1;
    VkDeviceSize size_class = dd->_next_power_of_2(logical);

    VkBufferUsageFlags usage_final = buffer_ci.usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    uint64_t key = _buffer_transient_key(usage_final, buffer_ci.device_local, buffer_ci.host_visible, buffer_ci.cpu_read, size_class);
    BufferTransientPool& pool = buffer_transient_pools[current_frame];

    drivers::DeviceDriverVulkan::Buffer buf;
    auto it = pool.free.find(key);
    if (it != pool.free.end() && !it->second.empty()) {
        buf = it->second.back();
        it->second.pop_back();
        ++buffer_reuse_hits;
    } else {
        drivers::DeviceDriverVulkan::BufferCreateInfo alloc_ci = buffer_ci;
        alloc_ci.size = size_class;
        buf = dd->buffer_create(alloc_ci);
        ++buffer_reuse_misses;
    }

    buf.size = logical;
    buf.state = {};

    r.transient_storage = buf;
    r.buffer = &r.transient_storage;
}

void RenderGraph::_buffer_release_transients()
{
    if (buffer_transient_pools.empty()) return;
    BufferTransientPool& pool = buffer_transient_pools[current_frame];
    for (BufferResource& r : buffer_resources) {
        if (r.kind != ResourceKind::Transient) continue;
        if (!r.buffer) continue;

        uint64_t key = _buffer_transient_key(r.transient_storage.usage, r.transient_storage.device_local, r.transient_storage.host_visible, r.transient_storage.cpu_read, r.transient_storage.capacity);
        pool.free[key].push_back(r.transient_storage);

        r.buffer = nullptr;
        r.transient_storage = {};
    }
}

/******************/
/**** COMMANDS ****/
/******************/

void RenderGraph::CommandList::draw(std::string_view p_name, uint32_t p_vertex_count, uint32_t p_instance_count, uint32_t p_base_vertex, uint32_t p_first_instance)
{
    graph->profiler.draw_begin(cmd, p_name, "Draw", p_instance_count);
    dd->command_render_draw(cmd, p_vertex_count, p_instance_count, p_base_vertex, p_first_instance);
    ++draw_count;
    graph->profiler.draw_end(cmd);
}

void RenderGraph::CommandList::draw_indexed_indirect_count(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, const drivers::DeviceDriverVulkan::Buffer& p_count, uint64_t p_count_offset, uint32_t p_max_draws, uint32_t p_stride)
{
    graph->profiler.draw_begin(cmd, p_name, "Draw Indexed Indirect Count", p_max_draws);
    dd->command_render_draw_indexed_indirect_count(cmd, p_indirect, p_offset, p_count, p_count_offset, p_max_draws, p_stride);
    ++draw_count;
    graph->profiler.draw_end(cmd);
}

void RenderGraph::CommandList::draw_indirect_count(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, const drivers::DeviceDriverVulkan::Buffer& p_count, uint64_t p_count_offset, uint32_t p_max_draws, uint32_t p_stride)
{
    graph->profiler.draw_begin(cmd, p_name, "Draw Indirect Count", p_max_draws);
    dd->command_render_draw_indirect_count(cmd, p_indirect, p_offset, p_count, p_count_offset, p_max_draws, p_stride);
    ++draw_count;
    graph->profiler.draw_end(cmd);
}

void RenderGraph::CommandList::dispatch(std::string_view p_name, uint32_t p_x, uint32_t p_y, uint32_t p_z)
{
    graph->profiler.dispatch_begin(cmd, p_name, "Dispatch");
    dd->command_compute_dispatch(cmd, p_x, p_y, p_z);
    ++draw_count;
    graph->profiler.dispatch_end(cmd);
}

void RenderGraph::CommandList::dispatch_indirect(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset)
{
    graph->profiler.dispatch_begin(cmd, p_name, "Indirect Dispatch");
    dd->command_compute_dispatch_indirect(cmd, p_indirect, p_offset);
    ++draw_count;
    graph->profiler.dispatch_end(cmd);
}

void RenderGraph::CommandList::fill_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_buffer, uint32_t p_value, VkDeviceSize p_offset, VkDeviceSize p_size)
{
    graph->profiler.transfer_begin(cmd, p_name, "Fill");
    dd->command_fill_buffer(cmd, p_buffer, p_value, p_offset, p_size);
    graph->profiler.transfer_end(cmd);
}

void RenderGraph::CommandList::update_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_buffer, const void* p_data, VkDeviceSize p_size, VkDeviceSize p_offset)
{
    graph->profiler.transfer_begin(cmd, p_name, "Update");
    dd->command_update_buffer(cmd, p_buffer, p_data, p_size, p_offset);
    graph->profiler.transfer_end(cmd);
}

/**************/
/**** PASS ****/
/**************/

void RenderGraph::Builder::create_image(std::string_view p_name, const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info) { graph->create_image(p_name, p_create_info); }

void RenderGraph::Builder::read_image(std::string_view p_name, VkImageLayout p_layout, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access)
{
    ImageAccess a;
    a.name_id = graph->intern_named(p_name);
    a.layout = p_layout;
    a.stage = p_stage;
    a.access = p_access;
    a.is_write = false;
    graph->nodes[node_index].image_accesses.push_back(a);
}

void RenderGraph::Builder::read_all_images(VkPipelineStageFlags2 p_stage)
{
    Node& node = graph->nodes[node_index];
    for (RenderGraph::ImageResource& r : graph->image_resources) {
        if (r.final_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) continue;

        bool already = false;
        for (ImageAccess& existing : node.image_accesses) if (existing.name_id == r.name_id) { already = true; break; }
        if (already) continue;

        ImageAccess a;
        a.name_id  = r.name_id;
        a.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a.stage = p_stage;
        a.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        a.is_write = false;
        node.image_accesses.push_back(a);
    }
}

void RenderGraph::Builder::write_image(std::string_view p_name, VkImageLayout p_layout, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access)
{
    ImageAccess a;
    a.name_id = graph->intern_named(p_name);
    a.layout = p_layout;
    a.stage = p_stage;
    a.access = p_access;
    a.is_write = true;
    graph->nodes[node_index].image_accesses.push_back(a);
}

void RenderGraph::Builder::color_attachment(std::string_view p_name, VkAttachmentLoadOp p_load, VkClearValue p_clear)
{
    ImageAccess a;
    a.name_id = graph->intern_named(p_name);
    a.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    a.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    if (p_load == VK_ATTACHMENT_LOAD_OP_LOAD) a.access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    a.is_write = true;
    a.is_attachment = true;
    a.load_op = p_load;
    a.clear = p_clear;
    graph->nodes[node_index].image_accesses.push_back(a);
}

void RenderGraph::Builder::depth_attachment_read(std::string_view p_name, VkAttachmentLoadOp p_load)
{
    ImageAccess a;
    a.name_id = graph->intern_named(p_name);
    a.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    a.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    a.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    a.is_write = false;
    a.is_attachment = true;
    a.is_depth = true;
    a.load_op = p_load;
    graph->nodes[node_index].image_accesses.push_back(a);
}

void RenderGraph::Builder::depth_attachment(std::string_view p_name, VkAttachmentLoadOp p_load, VkClearValue p_clear)
{
    ImageAccess a;
    a.name_id = graph->intern_named(p_name);
    a.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    a.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    a.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (p_load == VK_ATTACHMENT_LOAD_OP_LOAD) a.access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    a.is_write = true;
    a.is_attachment = true;
    a.is_depth = true;
    a.load_op = p_load;
    a.clear = p_clear;
    graph->nodes[node_index].image_accesses.push_back(a);
}

void RenderGraph::Builder::create_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::BufferCreateInfo& p_create_info) { graph->create_buffer(p_name, p_create_info); }

void RenderGraph::Builder::read_buffer(std::string_view p_name, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access)
{
    BufferAccess a;
    a.name_id = graph->intern_named(p_name);
    a.stage = p_stage;
    a.access = p_access;
    a.is_write = false;
    graph->nodes[node_index].buffer_accesses.push_back(a);
}

void RenderGraph::Builder::write_buffer(std::string_view p_name, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access)
{
    BufferAccess a;
    a.name_id = graph->intern_named(p_name);
    a.stage = p_stage;
    a.access = p_access;
    a.is_write = true;
    graph->nodes[node_index].buffer_accesses.push_back(a);
}

/***************/
/**** CACHE ****/
/***************/

VkRenderPass RenderGraph::_get_or_create_render_pass(Node& node)
{
    drivers::DeviceDriverVulkan::RenderPassCreateInfo render_pass_ci{};
    render_pass_ci.attachments.reserve(node.attachment_access_idx.size());

    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t v){ key ^= v; key *= 1099511628211ull; };

    for (int ai : node.attachment_access_idx) {
        ImageAccess& a = node.image_accesses[ai];
        auto& img = *image_resources[a.resource_index].image;

        drivers::DeviceDriverVulkan::RenderPassCreateInfo::Attachment att{};
        att.format = img.format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.load_op = a.load_op;
        att.store_op = a.store_op;
        att.initial_layout = (a.load_op == VK_ATTACHMENT_LOAD_OP_LOAD) ? img.state.layout : VK_IMAGE_LAYOUT_UNDEFINED;
        att.final_layout = a.layout;
        att.is_depth = a.is_depth;
        render_pass_ci.attachments.push_back(att);

        mix((uint64_t)att.format);
        mix((uint64_t)att.load_op);
        mix((uint64_t)att.store_op);
        mix((uint64_t)att.initial_layout);
        mix((uint64_t)att.final_layout);
        mix((uint64_t)att.is_depth);
    }

    if (auto it = render_pass_cache.find(key); it != render_pass_cache.end()) return it->second;

    render_pass_ci.name = "graph_render_pass";
    VkRenderPass rp = dd->render_pass_create(render_pass_ci);
    render_pass_cache[key] = rp;
    return rp;
}

VkRenderPass RenderGraph::acquire_render_pass(Pass& p_pass)
{
    const size_t node_mark = nodes.size();
    const size_t img_mark  = image_resources.size();
    const size_t buf_mark  = buffer_resources.size();

    nodes.push_back(Node{ &p_pass });
    if (p_pass.setup) {
        Builder b{ this, &p_pass, static_cast<uint32_t>(node_mark) };
        p_pass.setup(b);
    }

    struct Att { VkFormat format; bool is_depth; };
    std::vector<Att> atts;
    atts.reserve(nodes[node_mark].image_accesses.size());

    for (const ImageAccess& a : nodes[node_mark].image_accesses) {
        if (!a.is_attachment) continue;

        VkFormat fmt = VK_FORMAT_UNDEFINED;
        if (auto it = image_resource_map.find(a.name_id); it != image_resource_map.end()) {
            const ImageResource& r = image_resources[it->second];
            fmt = (r.kind == ResourceKind::Transient) ? r.image_create_info.format : (r.image ? r.image->format : VK_FORMAT_UNDEFINED);
        }
        if (fmt == VK_FORMAT_UNDEFINED) {
            if (auto it = declared_image_formats.find(a.name_id); it != declared_image_formats.end()) fmt = it->second;
        }
        if (fmt == VK_FORMAT_UNDEFINED) {
            log_write("RenderGraph: pass '%s' attaches '%s' with no resolvable format. If it is imported after pipeline creation, call declare_image_format().", p_pass.name.c_str(), debug_names[a.name_id].c_str());
        }
        atts.push_back({ fmt, a.is_depth });
    }

    for (size_t i = image_resources.size(); i-- > img_mark; ) image_resource_map.erase(image_resources[i].name_id);
    image_resources.resize(img_mark);
    for (size_t i = buffer_resources.size(); i-- > buf_mark; ) buffer_resource_map.erase(buffer_resources[i].name_id);
    buffer_resources.resize(buf_mark);
    nodes.resize(node_mark);

    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t v){ key ^= v; key *= 1099511628211ull; };
    mix(0xC0FFEEull);
    for (const Att& a : atts) {
        mix((uint64_t)a.format);
        mix((uint64_t)a.is_depth);
    }

    if (auto it = render_pass_cache.find(key); it != render_pass_cache.end()) return it->second;

    drivers::DeviceDriverVulkan::RenderPassCreateInfo render_pass_ci{};
    render_pass_ci.attachments.reserve(atts.size());
    for (const Att& a : atts) {
        drivers::DeviceDriverVulkan::RenderPassCreateInfo::Attachment att{};
        att.format = a.format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.store_op = VK_ATTACHMENT_STORE_OP_STORE;
        att.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.final_layout = a.is_depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.is_depth = a.is_depth;
        render_pass_ci.attachments.push_back(att);
    }
    render_pass_ci.name = "compat_render_pass";
    VkRenderPass rp = dd->render_pass_create(render_pass_ci);
    render_pass_cache[key] = rp;
    return rp;
}

VkFramebuffer RenderGraph::_get_or_create_framebuffer(Node& node)
{
    std::vector<VkImageView> views;
    views.reserve(node.attachment_access_idx.size());

    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { key ^= v; key *= 1099511628211ull; };
    mix((uint64_t)(uintptr_t)node.render_pass);

    for (int ai : node.attachment_access_idx) {
        auto& img = *image_resources[node.image_accesses[ai].resource_index].image;
        views.push_back(img.image_view);
        mix((uint64_t)(uintptr_t)img.image_view);
    }

    if (auto it = framebuffer_cache.find(key); it != framebuffer_cache.end()) return it->second;

    VkFramebuffer fb = dd->framebuffer_create(node.render_pass, views, node.area);
    framebuffer_cache[key] = fb;
    return fb;
}

/***************/
/**** GRAPH ****/
/***************/

void RenderGraph::begin(uint32_t p_current_frame)
{
    current_frame = p_current_frame;
    
    image_resources.clear();
    image_resources.reserve(64);
    image_resource_map.clear();
    final_image_barriers.clear();
    
    buffer_resources.clear();
    buffer_resources.reserve(64);
    buffer_resource_map.clear();
    final_buffer_barriers.clear();
    
    nodes.clear();
}

void RenderGraph::add(Pass* p_pass)
{
    Node node;
    node.pass = p_pass;
    uint32_t idx = static_cast<uint32_t>(nodes.size());
    nodes.push_back(std::move(node));

    if (p_pass->setup) {
        Builder builder{ this, p_pass, idx };
        p_pass->setup(builder);
    }
}

Error RenderGraph::compile()
{
    using enum Error;

    std::vector<int> img_writer(image_resources.size(), -1);
    std::vector<int> buf_writer(buffer_resources.size(), -1);

    constexpr VkAccessFlags2 READ_BITS =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT |
        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
        VK_ACCESS_2_MEMORY_READ_BIT;

    for (uint32_t n = 0; n < nodes.size(); ++n) {
        Node& node = nodes[n];

        for (ImageAccess& a : node.image_accesses) {
            auto it = image_resource_map.find(a.name_id);
            if (it == image_resource_map.end()) {
                log_write("RenderGraph: pass '%s' accesses unimported image '%s'.", node.pass->name.c_str(), debug_names[a.name_id].c_str());
                continue;
            }
            a.resource_index = static_cast<int>(it->second);
            ImageResource& r = image_resources[a.resource_index];
            if (r.first_use == -1) r.first_use = n;
            r.last_use = n;

            const bool loads = a.is_attachment && a.load_op == VK_ATTACHMENT_LOAD_OP_LOAD;
            const bool reads_existing = !a.is_write || (a.access & READ_BITS) != 0 || loads;
            if (reads_existing) {
                r.read = true;
                int prod = img_writer[a.resource_index];
                if (prod >= 0) node.deps.push_back(prod);
            }
            if (a.is_write) {
                r.written = true;
                img_writer[a.resource_index] = static_cast<int>(n);
            }
        }

        for (BufferAccess& a : node.buffer_accesses) {
            auto it = buffer_resource_map.find(a.name_id);
            if (it == buffer_resource_map.end()) {
                log_write("RenderGraph: pass '%s' accesses unimported buffer '%s'.", node.pass->name.c_str(), debug_names[a.name_id].c_str());
                continue;
            }
            a.resource_index = static_cast<int>(it->second);
            BufferResource& r = buffer_resources[a.resource_index];
            if (r.first_use == -1) r.first_use = n;
            r.last_use = n;

            const bool reads_existing = !a.is_write || (a.access & READ_BITS) != 0;
            if (reads_existing) {
                r.read = true;
                int prod = buf_writer[a.resource_index];
                if (prod >= 0) node.deps.push_back(prod);
            }
            if (a.is_write) {
                r.written = true;
                buf_writer[a.resource_index] = static_cast<int>(n);
            }
        }
    }

    for (ImageResource& r : image_resources) {
        if (r.read && !r.written && r.kind != ResourceKind::Imported) log_write("RenderGraph: '%s' read before write.", debug_names[r.name_id].c_str());
    }

    for (Node& node : nodes) node.culled = true;

    std::vector<uint32_t> worklist;
    for (uint32_t n = 0; n < nodes.size(); ++n) {
        bool root = nodes[n].pass->never_cull;

        for (ImageAccess& a : nodes[n].image_accesses) {
            if (!a.is_write || a.resource_index < 0) continue;
            ImageResource& r = image_resources[a.resource_index];
            if (r.kind == ResourceKind::Imported && r.final_layout != VK_IMAGE_LAYOUT_UNDEFINED) root = true;
        }
        for (BufferAccess& a : nodes[n].buffer_accesses) {
            if (!a.is_write || a.resource_index < 0) continue;
            BufferResource& r = buffer_resources[a.resource_index];
            if (r.kind == ResourceKind::Imported && r.final_access != 0) root = true;
        }

        if (root) {
            nodes[n].culled = false;
            worklist.push_back(n);
        }
    }

    while (!worklist.empty()) {
        uint32_t n = worklist.back();
        worklist.pop_back();

        for (int dep : nodes[n].deps) {
            if (nodes[dep].culled) {
                nodes[dep].culled = false;
                worklist.push_back(static_cast<uint32_t>(dep));
            }
        }
    }

    for (ImageResource& r : image_resources) if (r.kind == ResourceKind::Transient) { r.first_use = -1; r.last_use = -1; }
    for (int n = 0; n < (int)nodes.size(); ++n) {
        if (nodes[n].culled) continue;
        for (ImageAccess& a : nodes[n].image_accesses) {
            if (a.resource_index < 0) continue;
            ImageResource& r = image_resources[a.resource_index];
            if (r.kind != ResourceKind::Transient) continue;
            if (r.first_use < 0) r.first_use = n;
            r.last_use = n;
        }
    }

    std::unordered_map<uint64_t, std::vector<drivers::DeviceDriverVulkan::Image>> local_free;

    for (Node& node : nodes) {
        if (node.culled) continue;
        node.attachment_access_idx.clear();
        const int node_idx = (int)(&node - nodes.data());

        for (int i = 0; i < (int)node.image_accesses.size(); ++i) {
            ImageAccess& a = node.image_accesses[i];
            if (a.resource_index < 0) continue;
            ImageResource& r = image_resources[a.resource_index];

            if (r.kind == ResourceKind::Transient && !r.image) {
                uint32_t w, h; _image_resolve_extent(r.image_create_info, w, h);
                uint64_t key = _image_transient_key(r.image_create_info, VkExtent2D{ w, h });
                auto it = local_free.find(key);
                if (it != local_free.end() && !it->second.empty()) {
                    r.transient_storage = it->second.back();
                    it->second.pop_back();
                    r.image = &r.transient_storage;
                } else {
                    _image_materialize_transient(r);
                }
            }
            auto& img = *r.image;

            if (a.is_attachment) {
                const bool reused = img.state.layout != VK_IMAGE_LAYOUT_UNDEFINED || img.state.access != 0;
                if (reused) {
                    ImageBarrier b{};
                    b.image = img.image;
                    b.aspect = img.aspect;
                    b.old_layout = img.state.layout;
                    b.new_layout = a.layout;
                    b.src_stage = img.state.stage;
                    b.dst_stage = a.stage;
                    b.src_access = img.state.access;
                    b.dst_access = a.access;
                    node.pre_image_barriers.push_back(b);
                    img.state.layout = a.layout;
                    img.state.stage = a.stage;
                    img.state.access = a.access;
                }
                node.attachment_access_idx.push_back(i);
                if (node.area.width == 0) node.area = { img.extent.width, img.extent.height };
                continue;
            }

            constexpr VkAccessFlags2 WRITE_MASK =
                VK_ACCESS_2_SHADER_WRITE_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_WRITE_BIT |
                VK_ACCESS_2_HOST_WRITE_BIT |
                VK_ACCESS_2_MEMORY_WRITE_BIT;

            const bool layout_change = img.state.layout != a.layout;
            const bool prev_write = (img.state.access & WRITE_MASK) != 0;
            const bool curr_write = a.is_write || (a.access & WRITE_MASK) != 0;

            if (layout_change || prev_write || curr_write) {
                ImageBarrier b{};
                b.image = img.image;
                b.aspect = img.aspect;
                b.old_layout = img.state.layout;
                b.new_layout = a.layout;
                b.src_stage = img.state.stage;
                b.dst_stage = a.stage;
                b.src_access = img.state.access;
                b.dst_access = a.access;
                node.pre_image_barriers.push_back(b);
            }
            img.state.layout = a.layout;
            img.state.stage = a.stage;
            img.state.access = a.access;
        }

        for (BufferAccess& a : node.buffer_accesses) {
            if (a.resource_index < 0) continue;
            BufferResource& r = buffer_resources[a.resource_index];
            if (r.kind == ResourceKind::Transient && !r.buffer) _buffer_materialize_transient(r);
            if (!r.buffer) continue;
            auto& buf = *r.buffer;

            constexpr VkAccessFlags2 BUF_WRITE_MASK =
                VK_ACCESS_2_SHADER_WRITE_BIT |
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_WRITE_BIT |
                VK_ACCESS_2_HOST_WRITE_BIT |
                VK_ACCESS_2_MEMORY_WRITE_BIT;

            const bool prev_write = (buf.state.access & BUF_WRITE_MASK) != 0;
            const bool curr_write = a.is_write || (a.access & BUF_WRITE_MASK) != 0;

            if (prev_write || curr_write) {
                BufferBarrier b{};
                b.buffer = buf.buffer;
                b.offset = 0;
                b.size = VK_WHOLE_SIZE;
                b.src_stage = buf.state.stage;
                b.dst_stage = a.stage;
                b.src_access = buf.state.access;
                b.dst_access = a.access;
                node.pre_buffer_barriers.push_back(b);
            }
            buf.state.stage = a.stage;
            buf.state.access = a.access;
        }

        if (!node.attachment_access_idx.empty()) {
            node.has_render_pass = true;
            node.render_pass = _get_or_create_render_pass(node);
            node.framebuffer = _get_or_create_framebuffer(node);

            node.clear_values.clear();
            node.clear_values.reserve(node.attachment_access_idx.size());
            for (int ai : node.attachment_access_idx) node.clear_values.push_back(node.image_accesses[ai].clear);

            for (int ai : node.attachment_access_idx) {
                ImageAccess& a = node.image_accesses[ai];
                auto& img = *image_resources[a.resource_index].image;
                img.state.layout = a.layout;
                img.state.stage = a.stage;
                img.state.access = a.access;
            }
        }

        std::unordered_set<int> released;
        for (ImageAccess& a : node.image_accesses) {
            if (a.resource_index < 0) continue;
            ImageResource& r = image_resources[a.resource_index];
            if (r.kind != ResourceKind::Transient || !r.image) continue;
            if (r.last_use != node_idx) continue;
            if (!released.insert(a.resource_index).second) continue;
            uint32_t w, h;
            _image_resolve_extent(r.image_create_info, w, h);
            uint64_t key = _image_transient_key(r.image_create_info, VkExtent2D{ w, h });
            local_free[key].push_back(r.transient_storage);
        }
    }

    for (ImageResource& r : image_resources) {
        if (r.final_layout == VK_IMAGE_LAYOUT_UNDEFINED) continue;
        auto& img = *r.image;
        if (img.state.layout == r.final_layout) continue;

        ImageBarrier b{};
        b.image = img.image;
        b.aspect = img.aspect;
        b.old_layout = img.state.layout;
        b.new_layout = r.final_layout;
        b.src_stage = img.state.stage;
        b.dst_stage = r.final_stage;
        b.src_access = img.state.access;
        b.dst_access = r.final_access;
        final_image_barriers.push_back(b);

        img.state.layout = r.final_layout;
        img.state.stage = r.final_stage;
        img.state.access = r.final_access;
    }

    return Ok;
}

static void emit_barriers_images(VkCommandBuffer p_cmd, const std::vector<RenderGraph::ImageBarrier>& p_barriers)
{
    if (p_barriers.empty()) return;

    std::vector<VkImageMemoryBarrier2> vk_barriers;
    vk_barriers.reserve(p_barriers.size());
    for (const RenderGraph::ImageBarrier& b : p_barriers) {
        VkImageMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        mb.srcStageMask = b.src_stage;
        mb.srcAccessMask = b.src_access;
        mb.dstStageMask = b.dst_stage;
        mb.dstAccessMask = b.dst_access;
        mb.oldLayout = b.old_layout;
        mb.newLayout = b.new_layout;
        mb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.image = b.image;
        mb.subresourceRange = { b.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
        vk_barriers.push_back(mb);
    }

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(vk_barriers.size());
    dep.pImageMemoryBarriers = vk_barriers.data();
    vkCmdPipelineBarrier2(p_cmd, &dep);
}

static void emit_buffer_barriers(VkCommandBuffer p_cmd, const std::vector<RenderGraph::BufferBarrier>& p_barriers)
{
    if (p_barriers.empty()) return;

    std::vector<VkBufferMemoryBarrier2> vk_barriers;
    vk_barriers.reserve(p_barriers.size());
    for (const RenderGraph::BufferBarrier& b : p_barriers) {
        VkBufferMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        mb.srcStageMask = b.src_stage;
        mb.srcAccessMask = b.src_access;
        mb.dstStageMask = b.dst_stage;
        mb.dstAccessMask = b.dst_access;
        mb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.buffer = b.buffer;
        mb.offset = b.offset;
        mb.size = b.size;
        vk_barriers.push_back(mb);
    }

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(vk_barriers.size());
    dep.pBufferMemoryBarriers = vk_barriers.data();
    vkCmdPipelineBarrier2(p_cmd, &dep);
}

void RenderGraph::execute(VkCommandBuffer p_cmd)
{
    profiler.frame_begin(p_cmd, current_frame);

    for (uint32_t n = 0; n < nodes.size(); ++n) {
        Node& node = nodes[n];
        if (node.culled) continue;

        const bool has_barriers = !node.pre_image_barriers.empty() || !node.pre_buffer_barriers.empty();
        if (has_barriers) profiler.sync_begin(p_cmd, node.pass->name, node.pass->category);
        emit_barriers_images(p_cmd, node.pre_image_barriers);
        emit_buffer_barriers(p_cmd, node.pre_buffer_barriers);
        if (has_barriers) profiler.sync_end(p_cmd);

        profiler.pass_begin(p_cmd, node.pass->name, node.pass->category);
        if (node.has_render_pass) dd->command_begin_render_pass(p_cmd, node.render_pass, node.framebuffer, node.area, node.clear_values);
        CommandList cl;
        cl.cmd = p_cmd;
        cl.graph = this;
        cl.dd = dd;
        cl.node_index = n;
        if (node.pass->execute) node.pass->execute(cl);
        if (node.has_render_pass) dd->command_end_render_pass(p_cmd);
        profiler.pass_end(p_cmd, cl.draw_count);
    }

    emit_barriers_images(p_cmd, final_image_barriers);
    emit_buffer_barriers(p_cmd, final_buffer_barriers);

    profiler.frame_end();
    release_transients();
}

}