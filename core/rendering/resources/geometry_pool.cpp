#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/base/console.h>
#include <core/base/strings.h>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <fstream>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

static constexpr uint32_t PAGE_ABSENT = 0;
static constexpr uint32_t PAGE_STREAMED = 1;
static constexpr uint32_t PAGE_PERMANENT = 2;
static constexpr uint32_t PAGE_VERTEX_BYTES = 10;
static constexpr uint32_t PAGE_SKIN_BYTES = 4;

static VkDeviceSize _request_buffer_bytes()
{
    return (VkDeviceSize)(2 + 2 * GeometryPool::STREAM_REQUEST_CAPACITY) * sizeof(uint32_t);
}

Error GeometryPool::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count ? p_frame_count : 1;

    _arena_setup(ArenaKind::TRI_SLOTS, sizeof(uint32_t), 1u << 20, MAX_TRIS, 0, "geo_tri_slots");
    _arena_setup(ArenaKind::CLUSTERS, sizeof(Cluster), 64u << 10, MAX_CLUSTERS, 0, "geo_clusters");
    _arena_setup(ArenaKind::GROUPS, sizeof(ClusterGroup), 16u << 10, MAX_GROUPS, 0, "geo_groups");
    _arena_setup(ArenaKind::SKIN, sizeof(uint32_t), 64u << 10, MAX_SKIN_WORDS, 0, "geo_skin");
    _arena_setup(ArenaKind::MESHES, sizeof(LMesh), 1u << 10, MAX_MESHES, 0, "geo_meshes");
    _arena_setup(ArenaKind::CLUSTER_STREAMS, sizeof(LMeshClusterStreams), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_streams");
    _arena_setup(ArenaKind::LOCAL_TRIANGLES, sizeof(uint32_t), 1u << 20, MAX_TRIS, 0, "geo_local_triangles");
    _arena_setup(ArenaKind::POSITIONS, sizeof(uint32_t), 1u << 20, MAX_POSITION_WORDS, 0, "geo_positions");
    _arena_setup(ArenaKind::ATTRIBUTES, sizeof(uint32_t), 1u << 20, MAX_ATTRIBUTE_WORDS, 0, "geo_attributes");
    _arena_setup(ArenaKind::CLUSTER_SLOTS, sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_slots");
    _arena_setup(ArenaKind::CLUSTER_BOUNDS, 3 * sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_bounds");
    _arena_setup(ArenaKind::CLUSTER_CONES, sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_cones");
    _arena_setup(ArenaKind::NODES, sizeof(LMeshNode), 64u << 10, MAX_NODES, 0, "geo_nodes");
    _arena_setup(ArenaKind::PAGE_RESIDENT, sizeof(uint32_t), 16u << 10, MAX_PAGES, 0, "geo_page_resident");
    _arena_setup(ArenaKind::PAGE_STAMP, sizeof(uint32_t), 16u << 10, MAX_PAGES, 0, "geo_page_stamp");
    _arena_setup(ArenaKind::GROUP_REFINED, sizeof(uint32_t), 16u << 10, MAX_GROUPS, 0, "geo_group_refined");

    address_buffers.resize(frame_count);
    for (Buffer& b : address_buffers) {
        b = dd->buffer_create({ .size = sizeof(GeometryAddresses), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "geo_addresses" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "GeometryPool: address buffer allocation failed.");
        const GeometryAddresses zero{};
        dd->buffer_update(b, &zero, sizeof(zero));
        dd->buffer_flush(b, 0, sizeof(zero));
    }

    stream_requests = dd->buffer_create({ .size = _request_buffer_bytes(), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .device_local = true, .name = "geo_stream_requests" });
    LUMEN_ERR_FAIL_COND_V_MSG(!stream_requests.buffer, FAILED, "GeometryPool: stream request buffer allocation failed.");

    request_readback.resize(frame_count);
    request_written.assign(frame_count, 0);
    for (Buffer& b : request_readback) {
        b = dd->buffer_create({ .size = _request_buffer_bytes(), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .device_local = false, .host_visible = true, .cpu_read = true, .pool = dd->readback_pool, .name = "geo_stream_readback" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "GeometryPool: stream readback allocation failed.");
    }

    staging.resize(frame_count);
    for (StagingSlot& s : staging) {
        s.buffer = dd->buffer_create({ .size = STREAM_STAGING_BYTES, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .device_local = false, .host_visible = true, .pool = dd->upload_pool, .name = "geo_stream_staging" });
        LUMEN_ERR_FAIL_COND_V_MSG(!s.buffer.buffer, FAILED, "GeometryPool: stream staging allocation failed.");
    }

    reader.start();
    _register_commands();
    return OK;
}

void GeometryPool::_register_commands()
{
    console_register("geo.stream_budget", "[MB] show or set the mesh streaming budget", [this](const ConsoleArgs& p_args) {
        if (!p_args.empty()) {
            const double mb = std::strtod(std::string(p_args[0]).c_str(), nullptr);
            if (mb > 0.0) {
                stream_budget = (uint64_t)(mb * 1024.0 * 1024.0);
                _evict_scan_after = 0;
            }
        }
        log_write("geo.stream_budget = %.1f MB (resident %.1f MB)", (double)stream_budget / (1024.0 * 1024.0), (double)_stream_resident_bytes / (1024.0 * 1024.0));
    });
    console_register("geo.stream_stats", "print mesh streaming state", [this](const ConsoleArgs&) {
        const StreamStats& st = stream_stats;
        log_write("streaming: %.1f / %.1f MB, pages %u / %u, in flight %u, failed %u, last frame requests %u loads %u evictions %u", (double)st.resident_bytes / (1024.0 * 1024.0), (double)st.budget_bytes / (1024.0 * 1024.0), st.resident_pages, st.streamed_pages, st.in_flight, st.failed_pages, st.requests, st.loads, st.evictions);
        uint32_t streamed = 0, resident = 0;
        for (uint32_t i = 0; i < (uint32_t)streams.size(); i++) {
            if (!get(i)) continue;
            if (streams[i].streamed) streamed++;
            else resident++;
        }
        log_write("streaming: %u streamed meshes, %u fully resident meshes", streamed, resident);
    });
    console_register("geo.stream_flush", "evict every streamed page except root pages", [this](const ConsoleArgs&) {
        _stream_evict_all();
        log_write("streaming: flushed, resident %.1f MB", (double)_stream_resident_bytes / (1024.0 * 1024.0));
    });
}

void GeometryPool::_stream_evict_all()
{
    const std::vector<uint64_t> keys = _resident_list;
    for (uint64_t key : keys) {
        if (_staging_free() < 64 * 1024) break;
        const uint32_t mesh = (uint32_t)(key >> 32);
        const uint32_t page = (uint32_t)key;
        if (mesh < streams.size() && page < streams[mesh].pages.size() && streams[mesh].pages[page].state == PageState::RESIDENT) _page_evict(mesh, page);
    }
}

void GeometryPool::shutdown()
{
    console_unregister("geo.stream_budget");
    console_unregister("geo.stream_stats");
    console_unregister("geo.stream_flush");
    free();
    reader.stop();
    for (Buffer& b : address_buffers) dd->buffer_free(b);
    address_buffers.clear();
    dd->buffer_free(stream_requests);
    for (Buffer& b : request_readback) dd->buffer_free(b);
    request_readback.clear();
    request_written.clear();
    for (StagingSlot& s : staging) dd->buffer_free(s.buffer);
    staging.clear();
}

void GeometryPool::begin_frame(uint64_t p_frame_number, uint32_t p_slot, VkDeviceAddress p_slot_table, VkDeviceAddress p_materials)
{
    frame_number = p_frame_number;
    current_slot = p_slot;
    _collect(p_frame_number);
    _stream_tick();

    GeometryAddresses a{};
    a.tri_slots = _arena(ArenaKind::TRI_SLOTS).buffer.device_address;
    a.slot_table = p_slot_table;
    a.clusters = _arena(ArenaKind::CLUSTERS).buffer.device_address;
    a.groups = _arena(ArenaKind::GROUPS).buffer.device_address;
    a.skin = _arena(ArenaKind::SKIN).buffer.device_address;
    a.meshes = _arena(ArenaKind::MESHES).buffer.device_address;
    a.cluster_streams = _arena(ArenaKind::CLUSTER_STREAMS).buffer.device_address;
    a.local_triangles = _arena(ArenaKind::LOCAL_TRIANGLES).buffer.device_address;
    a.positions = _arena(ArenaKind::POSITIONS).buffer.device_address;
    a.attributes = _arena(ArenaKind::ATTRIBUTES).buffer.device_address;
    a.materials = p_materials;
    a.cluster_slots = _arena(ArenaKind::CLUSTER_SLOTS).buffer.device_address;
    a.cluster_bounds = _arena(ArenaKind::CLUSTER_BOUNDS).buffer.device_address;
    a.cluster_cones = _arena(ArenaKind::CLUSTER_CONES).buffer.device_address;
    a.nodes = _arena(ArenaKind::NODES).buffer.device_address;
    a.page_resident = _arena(ArenaKind::PAGE_RESIDENT).buffer.device_address;
    a.page_stamp = _arena(ArenaKind::PAGE_STAMP).buffer.device_address;
    a.group_refined = _arena(ArenaKind::GROUP_REFINED).buffer.device_address;
    a.stream_requests = stream_requests.device_address;
    a.stream_frame = (uint32_t)(frame_number + 1);
    a.stream_request_capacity = STREAM_REQUEST_CAPACITY;

    Buffer& b = address_buffers[p_slot];
    dd->buffer_update(b, &a, sizeof(a));
    dd->buffer_flush(b, 0, sizeof(a));

    cluster_extent = cluster_alloc.extent();
}

void GeometryPool::record_begin(VkCommandBuffer p_cmd)
{
    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;

    if (!_pending_growth.empty()) {
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier2(p_cmd, &dep);
        for (size_t i = 0; i < _pending_growth.size(); i++) {
            const PendingGrowth& g = _pending_growth[i];
            const VkBufferCopy region{ 0, 0, g.bytes };
            if (g.bytes) vkCmdCopyBuffer(p_cmd, g.src.buffer, g.dst.buffer, 1, &region);
            mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier2(p_cmd, &dep);
            Buffer src = g.src;
            _retire_buffer(src);
        }
        _pending_growth.clear();
    }

    vkCmdFillBuffer(p_cmd, stream_requests.buffer, 0, 2 * sizeof(uint32_t), 0);

    StagingSlot& st = staging[current_slot];
    if (!st.copies.empty()) {
        dd->buffer_flush(st.buffer, 0, st.used);
        std::vector<VkBufferCopy> regions;
        for (uint32_t k = 0; k < ARENA_COUNT; k++) {
            regions.clear();
            for (const StagedCopy& c : st.copies) {
                if ((uint32_t)c.kind == k) regions.push_back({ c.src, c.dst, c.size });
            }
            const Arena& a = arenas[k];
            if (!regions.empty() && a.buffer.buffer) vkCmdCopyBuffer(p_cmd, st.buffer.buffer, a.buffer.buffer, (uint32_t)regions.size(), regions.data());
        }
        st.copies.clear();
    }

    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier2(p_cmd, &dep);
}

void GeometryPool::record_end(VkCommandBuffer p_cmd)
{
    if (current_slot >= request_readback.size()) return;

    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(p_cmd, &dep);

    const VkBufferCopy region{ 0, 0, _request_buffer_bytes() };
    vkCmdCopyBuffer(p_cmd, stream_requests.buffer, request_readback[current_slot].buffer, 1, &region);

    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    vkCmdPipelineBarrier2(p_cmd, &dep);
    request_written[current_slot] = 1;
}

void GeometryPool::_arena_setup(ArenaKind p_kind, uint32_t p_stride, uint32_t p_initial, uint32_t p_max, VkBufferUsageFlags p_usage, const char* p_name)
{
    Arena& a = _arena(p_kind);
    a.stride = p_stride;
    a.initial = std::min(p_initial, p_max);
    a.max = p_max;
    a.usage = p_usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    a.name = p_name;
}

Error GeometryPool::_arena_create(Arena& r_arena, uint32_t p_capacity)
{
    using enum Error;
    r_arena.buffer = dd->buffer_create({ .size = (VkDeviceSize)p_capacity * r_arena.stride, .usage = r_arena.usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = r_arena.name });
    LUMEN_ERR_FAIL_COND_V_MSG(!r_arena.buffer.buffer, FAILED, "GeometryPool: arena allocation failed.");
    r_arena.capacity = p_capacity;
    return OK;
}

uint32_t GeometryPool::_grow_target(ArenaKind p_kind, uint64_t p_min) const
{
    const Arena& a = _arena(p_kind);
    if (p_min > a.max) return 0;
    const uint64_t doubled = a.capacity ? (uint64_t)a.capacity * 2 : a.initial;
    return (uint32_t)std::min<uint64_t>(std::max(doubled, p_min), a.max);
}

Error GeometryPool::_arena_grow(ArenaKind p_kind, uint32_t p_capacity, uint32_t p_live)
{
    using enum Error;
    Arena& a = _arena(p_kind);
    if (p_capacity <= a.capacity) return OK;

    if (!_deferred_growth) _flush_growth();

    Buffer old = a.buffer;
    const uint32_t old_capacity = a.capacity;
    if (_arena_create(a, p_capacity) != OK) {
        a.buffer = old;
        a.capacity = old_capacity;
        return FAILED;
    }

    if (_deferred_growth) {
        if (old.buffer) _pending_growth.push_back({ old, a.buffer, (VkDeviceSize)p_live * a.stride });
        return OK;
    }

    if (p_live && old.buffer) {
        const drivers::DeviceDriverVulkan::BufferCopy copy{ &old, &a.buffer, (VkDeviceSize)p_live * a.stride, 0, 0 };
        if (dd->buffer_copy_batch(&copy, 1) != OK) {
            dd->buffer_free(a.buffer);
            a.buffer = old;
            a.capacity = old_capacity;
            return FAILED;
        }
    }

    _retire_buffer(old);
    return OK;
}

void GeometryPool::_flush_growth()
{
    for (PendingGrowth& g : _pending_growth) {
        if (g.bytes) {
            const drivers::DeviceDriverVulkan::BufferCopy copy{ &g.src, &g.dst, g.bytes, 0, 0 };
            dd->buffer_copy_batch(&copy, 1);
        }
        _retire_buffer(g.src);
    }
    _pending_growth.clear();
}

bool GeometryPool::_ensure_tri_slots(uint32_t p_end)
{
    Arena& a = _arena(ArenaKind::TRI_SLOTS);
    if (p_end <= a.capacity) return true;
    const uint32_t capacity = _grow_target(ArenaKind::TRI_SLOTS, std::max<uint64_t>(p_end, tri_alloc.capacity));
    return capacity != 0 && _arena_grow(ArenaKind::TRI_SLOTS, capacity, a.capacity) == Error::OK;
}

uint32_t GeometryPool::_allocate(RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count)
{
    if (p_count == 0) return RangeAllocator::INVALID;

    const uint32_t offset = r_alloc.allocate(p_count);
    if (offset != RangeAllocator::INVALID) return offset;

    const uint32_t capacity = _grow_target(p_kind, (uint64_t)r_alloc.capacity + p_count - r_alloc.tail_free());
    if (capacity == 0) return RangeAllocator::INVALID;

    const uint32_t live = r_alloc.extent();
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_STREAMS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_SLOTS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_BOUNDS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_CONES, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::GROUPS && _arena_grow(ArenaKind::GROUP_REFINED, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::PAGE_RESIDENT && _arena_grow(ArenaKind::PAGE_STAMP, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (_arena_grow(p_kind, capacity, live) != Error::OK) return RangeAllocator::INVALID;

    r_alloc.grow(capacity);
    return r_alloc.allocate(p_count);
}

void GeometryPool::_release(const MeshRanges& p_r)
{
    position_alloc.free(p_r.position, p_r.position_count);
    attribute_alloc.free(p_r.attribute, p_r.attribute_count);
    tri_alloc.free(p_r.tri, p_r.tri_count);
    cluster_alloc.free(p_r.cluster, p_r.cluster_count);
    group_alloc.free(p_r.group, p_r.group_count);
    skin_alloc.free(p_r.skin, p_r.skin_count);
    node_alloc.free(p_r.node, p_r.node_count);
    page_alloc.free(p_r.page, p_r.page_count);
}

void GeometryPool::_retire_buffer(Buffer& r_buffer)
{
    if (!r_buffer.buffer) return;
    _retired_buffers.push_back({ frame_number, r_buffer });
    r_buffer = {};
}

void GeometryPool::_collect(uint64_t p_frame_number)
{
    for (size_t i = 0; i < _retired_meshes.size();) {
        if (_retired_meshes[i].frame + frame_count > p_frame_number) { i++; continue; }
        _release(_retired_meshes[i].ranges);
        free_meshes.push_back(_retired_meshes[i].id);
        _retired_meshes[i] = _retired_meshes.back();
        _retired_meshes.pop_back();
    }
    for (size_t i = 0; i < _retired_pages.size();) {
        if (_retired_pages[i].frame + frame_count > p_frame_number) { i++; continue; }
        tri_alloc.free(_retired_pages[i].tri, _retired_pages[i].tri_count);
        position_alloc.free(_retired_pages[i].position, _retired_pages[i].position_count);
        attribute_alloc.free(_retired_pages[i].attribute, _retired_pages[i].attribute_count);
        cluster_alloc.free(_retired_pages[i].cluster, _retired_pages[i].cluster_count);
        skin_alloc.free(_retired_pages[i].skin, _retired_pages[i].skin_count);
        _retired_pages[i] = _retired_pages.back();
        _retired_pages.pop_back();
    }
    for (size_t i = 0; i < _retired_buffers.size();) {
        if (_retired_buffers[i].frame + frame_count > p_frame_number) { i++; continue; }
        dd->buffer_free(_retired_buffers[i].buffer);
        _retired_buffers[i] = _retired_buffers.back();
        _retired_buffers.pop_back();
    }
}

VkDeviceSize GeometryPool::_resident_bytes() const
{
    VkDeviceSize total = 0;
    for (const Arena& a : arenas) total += a.buffer.capacity;
    for (const RetiredBuffer& r : _retired_buffers) total += r.buffer.capacity;
    return total;
}

void GeometryPool::free()
{
    for (PendingGrowth& g : _pending_growth) dd->buffer_free(g.src);
    _pending_growth.clear();
    for (Arena& a : arenas) {
        dd->buffer_free(a.buffer);
        a.buffer = {};
        a.capacity = 0;
    }
    for (RetiredBuffer& r : _retired_buffers) dd->buffer_free(r.buffer);
    _retired_buffers.clear();
    clear();
}

void GeometryPool::clear()
{
    reader.stop();
    reader.start();

    version++;
    meshes.clear();
    mesh_ranges.clear();
    mesh_guids.clear();
    mesh_joint_boxes.clear();
    mesh_rigs.clear();
    free_meshes.clear();
    by_guid.clear();
    _retired_meshes.clear();
    _retired_pages.clear();

    streams.clear();
    page_owner.clear();
    _resident_list.clear();
    _required.clear();
    _results.clear();
    _wanted.clear();
    _stream_resident_bytes = 0;
    _stream_loading_bytes = 0;
    _stream_total_pages = 0;
    _stream_resident_pages = 0;
    _stream_failed_pages = 0;
    _evict_scan_after = 0;
    _blocked.clear();
    _flush_growth();
    stream_stats = {};
    for (StagingSlot& s : staging) {
        s.copies.clear();
        s.used = 0;
    }
    std::fill(request_written.begin(), request_written.end(), (uint8_t)0);

    position_alloc.reset(_arena(ArenaKind::POSITIONS).capacity);
    attribute_alloc.reset(_arena(ArenaKind::ATTRIBUTES).capacity);
    tri_alloc.reset(_arena(ArenaKind::LOCAL_TRIANGLES).capacity);
    cluster_alloc.reset(_arena(ArenaKind::CLUSTERS).capacity);
    group_alloc.reset(_arena(ArenaKind::GROUPS).capacity);
    skin_alloc.reset(_arena(ArenaKind::SKIN).capacity);
    node_alloc.reset(_arena(ArenaKind::NODES).capacity);
    page_alloc.reset(_arena(ArenaKind::PAGE_RESIDENT).capacity);
    cluster_extent = 0;
    max_cluster_tris = 0;
    max_node_depth = 0;
}

uint32_t GeometryPool::_claim_id()
{
    uint32_t id = INVALID_MESH;
    if (!free_meshes.empty()) id = free_meshes.back();
    else if (meshes.size() < MAX_MESHES) id = (uint32_t)meshes.size();
    if (id == INVALID_MESH) return INVALID_MESH;
    if (id >= _arena(ArenaKind::MESHES).capacity) {
        const uint32_t capacity = _grow_target(ArenaKind::MESHES, (uint64_t)id + 1);
        if (capacity == 0 || _arena_grow(ArenaKind::MESHES, capacity, (uint32_t)meshes.size()) != Error::OK) return INVALID_MESH;
    }
    return id;
}

uint32_t GeometryPool::_commit_id(uint32_t p_id, Guid p_guid, const LMesh& p_mesh, const MeshRanges& p_ranges, std::vector<JointBox>&& r_boxes, LMeshRig&& r_rig)
{
    if (p_id == meshes.size()) {
        meshes.push_back(p_mesh);
        mesh_ranges.push_back(p_ranges);
        mesh_guids.push_back(p_guid);
        mesh_joint_boxes.push_back(std::move(r_boxes));
        mesh_rigs.push_back(std::move(r_rig));
    } else {
        free_meshes.pop_back();
        meshes[p_id] = p_mesh;
        mesh_ranges[p_id] = p_ranges;
        mesh_guids[p_id] = p_guid;
        mesh_joint_boxes[p_id] = std::move(r_boxes);
        mesh_rigs[p_id] = std::move(r_rig);
    }
    if (streams.size() < meshes.size()) streams.resize(meshes.size());
    if (page_owner.size() < page_alloc.capacity) page_owner.resize(page_alloc.capacity, INVALID_MESH);
    for (uint32_t i = 0; i < p_ranges.page_count; i++) page_owner[p_ranges.page + i] = p_id;
    by_guid.emplace(p_guid, p_id);
    version++;
    return p_id;
}

bool GeometryPool::_hierarchy_depth(std::vector<LMeshNode>& r_nodes, const MeshRanges& p_ranges, uint32_t& r_depth) const
{
    std::vector<uint32_t> depth(r_nodes.size(), 1u);
    r_depth = 1;
    for (uint32_t i = 0; i < (uint32_t)r_nodes.size(); i++) {
        LMeshNode& n = r_nodes[i];
        if (n.page == LMESH_NODE_INNER) {
            for (uint32_t k = n.first; k < n.first + n.count; k++) depth[k] = depth[i] + 1;
            n.first += p_ranges.node;
        } else {
            n.first += p_ranges.cluster;
        }
        r_depth = std::max(r_depth, depth[i]);
    }
    return r_depth <= MAX_NODE_DEPTH;
}

uint64_t GeometryPool::_page_gpu_bytes(const LMeshPage& p_page, bool p_wide, bool p_skinned)
{
    return (uint64_t)p_page.tri_count * sizeof(uint32_t) * (p_wide ? 2u : 1u) + (uint64_t)p_page.vertex_count * (PAGE_VERTEX_BYTES + (p_skinned ? PAGE_SKIN_BYTES : 0u)) + (uint64_t)p_page.cluster_count * (sizeof(Cluster) + sizeof(LMeshClusterStreams));
}

std::vector<GeometryPool::JointBox> GeometryPool::_joint_boxes(const std::vector<LMeshJointBox>& p_boxes)
{
    std::vector<JointBox> out(p_boxes.size());
    for (size_t i = 0; i < p_boxes.size(); i++) out[i] = { p_boxes[i].min, p_boxes[i].max };
    return out;
}

uint32_t GeometryPool::load(Guid p_guid, const std::filesystem::path& p_path)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end()) return it->second;

    _flush_growth();
    const std::string name = p_path.string();
    LMeshData data;
    uint32_t file_version = 0;
    std::string error;
    if (!lmesh_open(p_path, data, file_version, error)) {
        log_write("GeometryPool: %s: %s", name.c_str(), error.c_str());
        return INVALID_MESH;
    }
    const uint32_t id = _claim_id();
    if (id == INVALID_MESH) {
        log_write("GeometryPool: %s: mesh table full", name.c_str());
        return INVALID_MESH;
    }

    const bool streamed = file_version >= LMESH_VERSION_STREAMED && (!data.skinned() || !data.joint_boxes.empty());
    if (streamed) return _load_streamed(p_guid, p_path, data, id);
    if (!data.has_page_data() && !lmesh_load(p_path, data, error)) {
        log_write("GeometryPool: %s: %s", name.c_str(), error.c_str());
        return INVALID_MESH;
    }
    return _load_resident(p_guid, name, data, id);
}

uint32_t GeometryPool::_load_resident(Guid p_guid, const std::string& p_name, LMeshData& r_data, uint32_t p_id)
{
    auto fail = [&](const char* p_why) { log_write("GeometryPool: %s: %s", p_name.c_str(), p_why); return INVALID_MESH; };
    const LMeshPayloadHeader& h = r_data.header;
    const bool skinned = r_data.skinned();

    std::vector<uint32_t> positions, attributes, skin;
    std::vector<LMeshClusterStreams> cluster_streams(h.cluster_count);
    std::vector<uint32_t> cluster_cones(h.cluster_count, LMESH_CONE_NONE);
    {
        LMeshPageData slice;
        LMeshPackedPage packed;
        for (uint32_t p = 0; p < h.page_count; p++) {
            lmesh_page_slice(r_data, p, slice);
            lmesh_pack_page(r_data, p, slice, packed);
            _rebase_streams(packed.clusters, (uint32_t)positions.size(), (uint32_t)attributes.size(), (uint32_t)skin.size());
            std::copy(packed.clusters.begin(), packed.clusters.end(), cluster_streams.begin() + r_data.pages[p].cluster_first);
            std::copy(packed.cones.begin(), packed.cones.end(), cluster_cones.begin() + r_data.pages[p].cluster_first);
            positions.insert(positions.end(), packed.positions.begin(), packed.positions.end());
            attributes.insert(attributes.end(), packed.attributes.begin(), packed.attributes.end());
            skin.insert(skin.end(), packed.skin.begin(), packed.skin.end());
        }
    }

    MeshRanges r;
    r.position_count = (uint32_t)positions.size();
    r.attribute_count = (uint32_t)attributes.size();
    r.tri_count = h.tri_count;
    r.cluster_count = h.cluster_count;
    r.group_count = h.group_count;
    r.skin_count = skinned ? (uint32_t)skin.size() : 0u;
    r.node_count = h.node_count;
    r.page_count = h.page_count;

    auto reserve = [&](uint32_t& r_offset, RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count) {
        if (p_count == 0) return true;
        r_offset = _allocate(r_alloc, p_kind, p_count);
        return r_offset != RangeAllocator::INVALID;
    };
    const bool reserved =
        reserve(r.position, position_alloc, ArenaKind::POSITIONS, r.position_count) &&
        reserve(r.attribute, attribute_alloc, ArenaKind::ATTRIBUTES, r.attribute_count) &&
        reserve(r.tri, tri_alloc, ArenaKind::LOCAL_TRIANGLES, r.tri_count) &&
        reserve(r.cluster, cluster_alloc, ArenaKind::CLUSTERS, r.cluster_count) &&
        reserve(r.group, group_alloc, ArenaKind::GROUPS, r.group_count) &&
        reserve(r.skin, skin_alloc, ArenaKind::SKIN, r.skin_count) &&
        reserve(r.node, node_alloc, ArenaKind::NODES, r.node_count) &&
        reserve(r.page, page_alloc, ArenaKind::PAGE_RESIDENT, r.page_count);
    if (!reserved || (!r_data.tri_slots.empty() && !_ensure_tri_slots(r.tri + r.tri_count))) {
        _release(r);
        return fail("geometry arenas exhausted");
    }
    _rebase_streams(cluster_streams, r.position_count ? r.position : 0u, r.attribute_count ? r.attribute : 0u, r.skin_count ? r.skin : 0u);

    auto rebase_group = [&](uint32_t p_group) { return p_group == CLUSTER_GROUP_NONE ? CLUSTER_GROUP_NONE : p_group + r.group; };
    std::vector<Cluster> clusters(h.cluster_count);
    std::vector<uint32_t> cluster_slots(h.cluster_count);
    std::vector<uint32_t> cluster_bounds((size_t)h.cluster_count * 3);
    uint32_t mesh_max_tris = 0;
    for (const LMeshPage& page : r_data.pages) {
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
            const LMeshCluster& c = r_data.clusters[ci];
            clusters[ci] = Cluster{ (r.tri + page.tri_first + c.tri_offset) * 3u, c.tri_count * 3u, c.cull_sphere, rebase_group(c.self_group), rebase_group(c.parent_group) };
            cluster_slots[ci] = c.slot;
            std::memcpy(&cluster_bounds[(size_t)ci * 3], c.bounds, sizeof(c.bounds));
            mesh_max_tris = std::max(mesh_max_tris, c.tri_count);
        }
    }

    std::vector<LMeshNode> nodes = r_data.nodes;
    uint32_t mesh_depth = 1;
    if (!_hierarchy_depth(nodes, r, mesh_depth)) {
        _release(r);
        return fail("cluster hierarchy is deeper than the traversal supports");
    }

    std::vector<JointBox> joint_boxes = _joint_boxes(r_data.joint_boxes);
    if (skinned && joint_boxes.empty()) {
        for (uint32_t v = 0; v < h.vertex_count; v++) {
            const SkinVertex& sv = r_data.skin[v];
            const vec3 pos = h.pos_min + vec3(r_data.vertices[v].position.position) / 65535.0f * h.pos_extent;
            for (int k = 0; k < 4; k++) {
                if (sv.weights[k] == 0) continue;
                const uint32_t j = sv.joints[k];
                if (j >= joint_boxes.size()) joint_boxes.resize(j + 1);
                joint_boxes[j].min = glm::min(joint_boxes[j].min, pos);
                joint_boxes[j].max = glm::max(joint_boxes[j].max, pos);
            }
        }
    }

    const std::vector<uint32_t> page_resident(h.page_count, PAGE_PERMANENT);
    const std::vector<uint32_t> page_stamp(h.page_count, 0u);
    const std::vector<uint32_t> group_refined(h.group_count, 1u);

    LMesh m{};
    m.cluster_base = r.cluster_count ? r.cluster : 0;
    m.cluster_count = h.cluster_count;
    m.slot_count = h.slot_count;
    m.node_base = r.node_count ? r.node : 0;
    m.node_count = h.node_count;
    m.page_base = r.page_count ? r.page : 0;
    m.page_count = h.page_count;
    m.pos_min = h.pos_min;
    m.pos_extent = h.pos_extent;
    m.uv_min = h.uv_min;
    m.uv_extent = h.uv_extent;
    m.bounds_sphere = h.bounds_sphere;

    std::vector<drivers::DeviceDriverVulkan::BufferUpload> uploads;
    auto add = [&](ArenaKind p_kind, uint32_t p_offset, const void* p_data, size_t p_bytes) {
        if (p_bytes) uploads.push_back({ &_arena(p_kind).buffer, p_data, (VkDeviceSize)p_bytes, (VkDeviceSize)p_offset * _arena(p_kind).stride });
    };
    add(ArenaKind::POSITIONS, r.position, positions.data(), positions.size() * sizeof(uint32_t));
    add(ArenaKind::ATTRIBUTES, r.attribute, attributes.data(), attributes.size() * sizeof(uint32_t));
    add(ArenaKind::LOCAL_TRIANGLES, r.tri, r_data.local_tris.data(), r_data.local_tris.size() * sizeof(uint32_t));
    add(ArenaKind::TRI_SLOTS, r.tri, r_data.tri_slots.data(), r_data.tri_slots.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTERS, r.cluster, clusters.data(), clusters.size() * sizeof(Cluster));
    add(ArenaKind::CLUSTER_STREAMS, r.cluster, cluster_streams.data(), cluster_streams.size() * sizeof(LMeshClusterStreams));
    add(ArenaKind::CLUSTER_CONES, r.cluster, cluster_cones.data(), cluster_cones.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTER_SLOTS, r.cluster, cluster_slots.data(), cluster_slots.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTER_BOUNDS, r.cluster, cluster_bounds.data(), cluster_bounds.size() * sizeof(uint32_t));
    add(ArenaKind::GROUPS, r.group, r_data.groups.data(), r_data.groups.size() * sizeof(ClusterGroup));
    add(ArenaKind::GROUP_REFINED, r.group, group_refined.data(), group_refined.size() * sizeof(uint32_t));
    if (r.skin_count) add(ArenaKind::SKIN, r.skin, skin.data(), skin.size() * sizeof(uint32_t));
    add(ArenaKind::NODES, r.node, nodes.data(), nodes.size() * sizeof(LMeshNode));
    add(ArenaKind::PAGE_RESIDENT, r.page, page_resident.data(), page_resident.size() * sizeof(uint32_t));
    add(ArenaKind::PAGE_STAMP, r.page, page_stamp.data(), page_stamp.size() * sizeof(uint32_t));
    add(ArenaKind::MESHES, p_id, &m, sizeof(LMesh));

    if (dd->buffer_upload_batch(uploads.data(), (uint32_t)uploads.size()) != Error::OK) {
        _release(r);
        return fail("upload failed");
    }

    _commit_id(p_id, p_guid, m, r, std::move(joint_boxes), std::move(r_data.rig));
    streams[p_id] = StreamMesh{};
    max_cluster_tris = std::max(max_cluster_tris, mesh_max_tris);
    max_node_depth = std::max(max_node_depth, mesh_depth);

    log_write("GeometryPool: loaded %s resident tris=%u clusters=%u groups=%u nodes=%u depth=%u vertex_bytes=%u skin_bytes=%u id=%u", p_name.c_str(), h.tri_count, h.cluster_count, h.group_count, h.node_count, mesh_depth, (uint32_t)((positions.size() + attributes.size()) * sizeof(uint32_t)), (uint32_t)(skin.size() * sizeof(uint32_t)), p_id);
    return p_id;
}

uint32_t GeometryPool::_load_streamed(Guid p_guid, const std::filesystem::path& p_path, LMeshData& r_tables, uint32_t p_id)
{
    const std::string name = p_path.string();
    auto fail = [&](const char* p_why) { log_write("GeometryPool: %s: %s", name.c_str(), p_why); return INVALID_MESH; };
    const LMeshPayloadHeader& h = r_tables.header;
    const bool wide = r_tables.wide_slots();
    const bool skinned = r_tables.skinned();

    MeshRanges r;
    r.group_count = h.group_count;
    r.node_count = h.node_count;
    r.page_count = h.page_count;

    auto reserve = [&](uint32_t& r_offset, RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count) {
        if (p_count == 0) return true;
        r_offset = _allocate(r_alloc, p_kind, p_count);
        return r_offset != RangeAllocator::INVALID;
    };
    const bool reserved =
        reserve(r.group, group_alloc, ArenaKind::GROUPS, r.group_count) &&
        reserve(r.node, node_alloc, ArenaKind::NODES, r.node_count) &&
        reserve(r.page, page_alloc, ArenaKind::PAGE_RESIDENT, r.page_count);
    if (!reserved) {
        _release(r);
        return fail("geometry arenas exhausted");
    }

    std::vector<LMeshNode> nodes = r_tables.nodes;
    uint32_t mesh_depth = 1;
    if (!_hierarchy_depth(nodes, r, mesh_depth)) {
        _release(r);
        return fail("cluster hierarchy is deeper than the traversal supports");
    }

    StreamMesh s;
    s.streamed = true;
    s.generation = ++_stream_generation;
    s.pages.resize(h.page_count);
    s.group_missing.resize(h.group_count);
    std::vector<uint32_t> per_page(h.page_count + 1, 0u);
    for (uint32_t g = 0; g < h.group_count; g++) {
        s.group_missing[g] = r_tables.group_child_first[g + 1] - r_tables.group_child_first[g];
        for (uint32_t k = r_tables.group_child_first[g]; k < r_tables.group_child_first[g + 1]; k++) per_page[r_tables.group_child_pages[k] + 1]++;
    }
    for (uint32_t p = 0; p < h.page_count; p++) per_page[p + 1] += per_page[p];
    s.page_groups_first = per_page;
    s.page_groups.resize(per_page[h.page_count]);
    for (uint32_t g = 0; g < h.group_count; g++) {
        for (uint32_t k = r_tables.group_child_first[g]; k < r_tables.group_child_first[g + 1]; k++) s.page_groups[per_page[r_tables.group_child_pages[k]]++] = g;
    }
    s.page_leaves_first.assign(h.page_count + 1, 0u);
    for (const LMeshNode& n : r_tables.nodes) {
        if (n.page != LMESH_NODE_INNER) s.page_leaves_first[n.page + 1]++;
    }
    for (uint32_t p = 0; p < h.page_count; p++) s.page_leaves_first[p + 1] += s.page_leaves_first[p];
    s.page_leaves.resize(s.page_leaves_first[h.page_count]);
    {
        std::vector<uint32_t> cursor(s.page_leaves_first.begin(), s.page_leaves_first.end() - 1);
        for (uint32_t i = 0; i < h.node_count; i++) {
            const LMeshNode& n = r_tables.nodes[i];
            if (n.page != LMESH_NODE_INNER) s.page_leaves[cursor[n.page]++] = i;
        }
    }

    std::vector<LMeshPageData> root_data(h.root_page_count);
    std::vector<LMeshPackedPage> root_packed(h.root_page_count);
    {
        std::ifstream f(p_path, std::ios::binary);
        std::vector<uint8_t> blob;
        for (uint32_t p = 0; p < h.root_page_count; p++) {
            const LMeshPage& info = r_tables.pages[p];
            LMeshPageData& page = root_data[p];
            blob.resize(info.size);
            f.seekg((std::streamoff)info.offset);
            const char* why = !f.read(reinterpret_cast<char*>(blob.data()), (std::streamsize)info.size) ? "truncated root page" : lmesh_decode_page(blob.data(), blob.size(), info, h.flags, page);
            if (!why) why = lmesh_validate_page(r_tables, p, page);
            StreamPage& sp = s.pages[p];
            if (!why) {
                lmesh_pack_page(r_tables, p, page, root_packed[p]);
                if (!_allocate_page(sp, info, root_packed[p], wide)) why = "geometry arenas exhausted";
            }
            if (why) {
                for (uint32_t q = 0; q < p; q++) {
                    _free_page(s.pages[q], r_tables.pages[q]);
                    _stream_resident_bytes -= std::min(_stream_resident_bytes, _page_gpu_bytes(r_tables.pages[q], wide, skinned));
                }
                _release(r);
                return fail(why);
            }
            sp.state = PageState::RESIDENT;
            sp.last_used = frame_number;
            for (uint32_t k = s.page_groups_first[p]; k < s.page_groups_first[p + 1]; k++) s.group_missing[s.page_groups[k]]--;
            s.resident_pages++;
            _stream_resident_bytes += _page_gpu_bytes(info, wide, skinned);
        }
    }

    uint32_t mesh_max_tris = 0;
    for (const LMeshCluster& c : r_tables.clusters) mesh_max_tris = std::max(mesh_max_tris, c.tri_count);

    std::vector<std::vector<Cluster>> root_clusters(h.root_page_count);
    std::vector<std::vector<uint32_t>> root_cslots(h.root_page_count), root_bounds(h.root_page_count);
    for (uint32_t p = 0; p < h.root_page_count; p++) {
        const StreamPage& sp = s.pages[p];
        _page_clusters(r_tables, p, sp.tri, r.group, root_clusters[p], root_cslots[p], root_bounds[p]);
        _rebase_streams(root_packed[p].clusters, sp.position, sp.attribute, sp.skin);
    }

    for (uint32_t i = 0; i < h.node_count; i++) {
        const LMeshNode& src = r_tables.nodes[i];
        if (src.page == LMESH_NODE_INNER) continue;
        const StreamPage& sp = s.pages[src.page];
        nodes[i].first = sp.state == PageState::RESIDENT ? sp.cluster + (src.first - r_tables.pages[src.page].cluster_first) : 0u;
    }

    std::vector<uint32_t> page_resident(h.page_count, PAGE_ABSENT);
    for (uint32_t p = 0; p < h.root_page_count; p++) page_resident[p] = PAGE_PERMANENT;
    const std::vector<uint32_t> page_stamp(h.page_count, 0u);
    std::vector<uint32_t> group_refined(h.group_count);
    for (uint32_t g = 0; g < h.group_count; g++) group_refined[g] = s.group_missing[g] == 0 ? 1u : 0u;

    LMesh m{};
    m.cluster_base = 0;
    m.cluster_count = h.cluster_count;
    m.slot_count = h.slot_count;
    m.node_base = r.node;
    m.node_count = h.node_count;
    m.page_base = r.page;
    m.page_count = h.page_count;
    m.flags = MESH_FLAG_STREAMED;
    m.pos_min = h.pos_min;
    m.pos_extent = h.pos_extent;
    m.uv_min = h.uv_min;
    m.uv_extent = h.uv_extent;
    m.bounds_sphere = h.bounds_sphere;

    std::vector<drivers::DeviceDriverVulkan::BufferUpload> uploads;
    auto add = [&](ArenaKind p_kind, uint32_t p_offset, const void* p_data, size_t p_bytes) {
        if (p_bytes) uploads.push_back({ &_arena(p_kind).buffer, p_data, (VkDeviceSize)p_bytes, (VkDeviceSize)p_offset * _arena(p_kind).stride });
    };
    for (uint32_t p = 0; p < h.root_page_count; p++) {
        const StreamPage& sp = s.pages[p];
        const LMeshPageData& page = root_data[p];
        const LMeshPackedPage& packed = root_packed[p];
        add(ArenaKind::LOCAL_TRIANGLES, sp.tri, page.tris.data(), page.tris.size() * sizeof(uint32_t));
        if (wide) add(ArenaKind::TRI_SLOTS, sp.tri, page.tri_slots.data(), page.tri_slots.size() * sizeof(uint32_t));
        add(ArenaKind::POSITIONS, sp.position, packed.positions.data(), packed.positions.size() * sizeof(uint32_t));
        add(ArenaKind::ATTRIBUTES, sp.attribute, packed.attributes.data(), packed.attributes.size() * sizeof(uint32_t));
        add(ArenaKind::SKIN, sp.skin, packed.skin.data(), packed.skin.size() * sizeof(uint32_t));
        add(ArenaKind::CLUSTERS, sp.cluster, root_clusters[p].data(), root_clusters[p].size() * sizeof(Cluster));
        add(ArenaKind::CLUSTER_STREAMS, sp.cluster, packed.clusters.data(), packed.clusters.size() * sizeof(LMeshClusterStreams));
        add(ArenaKind::CLUSTER_CONES, sp.cluster, packed.cones.data(), packed.cones.size() * sizeof(uint32_t));
        add(ArenaKind::CLUSTER_SLOTS, sp.cluster, root_cslots[p].data(), root_cslots[p].size() * sizeof(uint32_t));
        add(ArenaKind::CLUSTER_BOUNDS, sp.cluster, root_bounds[p].data(), root_bounds[p].size() * sizeof(uint32_t));
    }
    add(ArenaKind::GROUPS, r.group, r_tables.groups.data(), r_tables.groups.size() * sizeof(ClusterGroup));
    add(ArenaKind::GROUP_REFINED, r.group, group_refined.data(), group_refined.size() * sizeof(uint32_t));
    add(ArenaKind::NODES, r.node, nodes.data(), nodes.size() * sizeof(LMeshNode));
    add(ArenaKind::PAGE_RESIDENT, r.page, page_resident.data(), page_resident.size() * sizeof(uint32_t));
    add(ArenaKind::PAGE_STAMP, r.page, page_stamp.data(), page_stamp.size() * sizeof(uint32_t));
    add(ArenaKind::MESHES, p_id, &m, sizeof(LMesh));

    if (dd->buffer_upload_batch(uploads.data(), (uint32_t)uploads.size()) != Error::OK) {
        for (uint32_t p = 0; p < h.root_page_count; p++) {
            _free_page(s.pages[p], r_tables.pages[p]);
            _stream_resident_bytes -= std::min(_stream_resident_bytes, _page_gpu_bytes(r_tables.pages[p], wide, skinned));
        }
        _release(r);
        return fail("upload failed");
    }

    auto source = std::make_shared<MeshPageReader::Source>();
    source->path = p_path;
    source->flags = h.flags;
    const uint32_t page_count = h.page_count;
    const uint32_t root_count = h.root_page_count;
    const uint32_t tri_count = h.tri_count;
    const uint32_t cluster_count = h.cluster_count;
    LMeshRig rig = std::move(r_tables.rig);
    std::vector<JointBox> joint_boxes = _joint_boxes(r_tables.joint_boxes);
    source->tables = std::make_shared<const LMeshData>(std::move(r_tables));
    s.source = std::move(source);

    _stream_total_pages += page_count;
    _stream_resident_pages += s.resident_pages;
    _commit_id(p_id, p_guid, m, r, std::move(joint_boxes), std::move(rig));
    streams[p_id] = std::move(s);
    max_cluster_tris = std::max(max_cluster_tris, mesh_max_tris);
    max_node_depth = std::max(max_node_depth, mesh_depth);

    log_write("GeometryPool: loaded %s streamed tris=%u clusters=%u pages=%u (%u root) depth=%u id=%u", name.c_str(), tri_count, cluster_count, page_count, root_count, mesh_depth, p_id);
    return p_id;
}

void GeometryPool::_stream_forget(uint32_t p_mesh)
{
    if (p_mesh >= streams.size()) return;
    StreamMesh& s = streams[p_mesh];
    if (!s.streamed) {
        s = StreamMesh{};
        return;
    }
    _stream_loading_bytes -= std::min(_stream_loading_bytes, reader.cancel(p_mesh));
    const bool wide = has_flag((MeshFlags)s.source->flags, MeshFlags::WIDE_SLOTS);
    const bool skinned = has_flag((MeshFlags)s.source->flags, MeshFlags::SKINNED);
    for (uint32_t p = 0; p < (uint32_t)s.pages.size(); p++) {
        StreamPage& sp = s.pages[p];
        if (sp.state != PageState::RESIDENT) continue;
        const LMeshPage& info = s.source->tables->pages[p];
        _retire_page(sp, info);
        _stream_resident_bytes -= std::min(_stream_resident_bytes, _page_gpu_bytes(info, wide, skinned));
        if (sp.list_index != UINT32_MAX) {
            const uint64_t moved = _resident_list.back();
            _resident_list[sp.list_index] = moved;
            streams[(uint32_t)(moved >> 32)].pages[(uint32_t)moved].list_index = sp.list_index;
            _resident_list.pop_back();
            sp.list_index = UINT32_MAX;
        }
    }
    _required.erase(std::remove(_required.begin(), _required.end(), p_mesh), _required.end());
    _stream_total_pages -= std::min<uint32_t>(_stream_total_pages, (uint32_t)s.pages.size());
    _stream_resident_pages -= std::min(_stream_resident_pages, s.resident_pages);
    _stream_failed_pages -= std::min(_stream_failed_pages, s.failed_pages);
    s = StreamMesh{};
}

void GeometryPool::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    const uint32_t id = it->second;
    by_guid.erase(it);
    version++;

    _stream_forget(id);
    const MeshRanges& r = mesh_ranges[id];
    for (uint32_t i = 0; i < r.page_count && r.page + i < page_owner.size(); i++) page_owner[r.page + i] = INVALID_MESH;
    _retired_meshes.push_back({ frame_number, id, r });
    meshes[id] = LMesh{};
    mesh_ranges[id] = MeshRanges{};
    mesh_guids[id] = Guid{};
    mesh_joint_boxes[id].clear();
    mesh_rigs[id] = {};
}

VkDeviceSize GeometryPool::_staging_free() const
{
    if (current_slot >= staging.size()) return 0;
    const VkDeviceSize used = (staging[current_slot].used + 15) & ~(VkDeviceSize)15;
    return used >= STREAM_STAGING_BYTES ? 0 : STREAM_STAGING_BYTES - used;
}

bool GeometryPool::_stage(ArenaKind p_kind, uint32_t p_offset, const void* p_data, VkDeviceSize p_bytes)
{
    return _stage_bytes(p_kind, (VkDeviceSize)p_offset * _arena(p_kind).stride, p_data, p_bytes);
}

bool GeometryPool::_stage_bytes(ArenaKind p_kind, VkDeviceSize p_dst, const void* p_data, VkDeviceSize p_bytes)
{
    if (p_bytes == 0) return true;
    if (current_slot >= staging.size()) return false;
    StagingSlot& st = staging[current_slot];
    const VkDeviceSize at = (st.used + 15) & ~(VkDeviceSize)15;
    if (at + p_bytes > STREAM_STAGING_BYTES) return false;
    dd->buffer_update(st.buffer, p_data, p_bytes, at);
    st.copies.push_back({ p_kind, at, p_dst, p_bytes });
    st.used = at + p_bytes;
    stream_stats.uploaded_bytes += p_bytes;
    return true;
}

void GeometryPool::_page_clusters(const LMeshData& p_tables, uint32_t p_page, uint32_t p_tri, uint32_t p_group_base, std::vector<Cluster>& r_clusters, std::vector<uint32_t>& r_slots, std::vector<uint32_t>& r_bounds) const
{
    const LMeshPage& info = p_tables.pages[p_page];
    auto rebase_group = [&](uint32_t p_group) { return p_group == CLUSTER_GROUP_NONE ? CLUSTER_GROUP_NONE : p_group + p_group_base; };
    r_clusters.resize(info.cluster_count);
    r_slots.resize(info.cluster_count);
    r_bounds.resize((size_t)info.cluster_count * 3);
    for (uint32_t i = 0; i < info.cluster_count; i++) {
        const LMeshCluster& c = p_tables.clusters[info.cluster_first + i];
        r_clusters[i] = Cluster{ (p_tri + c.tri_offset) * 3u, c.tri_count * 3u, c.cull_sphere, rebase_group(c.self_group), rebase_group(c.parent_group) };
        r_slots[i] = c.slot;
        std::memcpy(&r_bounds[(size_t)i * 3], c.bounds, sizeof(c.bounds));
    }
}

void GeometryPool::_rebase_streams(std::vector<LMeshClusterStreams>& r_streams, uint32_t p_position, uint32_t p_attribute, uint32_t p_skin)
{
    const uint32_t position = p_position == RangeAllocator::INVALID ? 0u : p_position;
    const uint32_t attribute = p_attribute == RangeAllocator::INVALID ? 0u : p_attribute;
    const uint32_t skin = p_skin == RangeAllocator::INVALID ? 0u : p_skin;
    for (LMeshClusterStreams& c : r_streams) {
        c.position += position;
        c.attribute += attribute;
        c.skin += skin;
    }
}

bool GeometryPool::_allocate_page(StreamPage& r_page, const LMeshPage& p_info, const LMeshPackedPage& p_packed, bool p_wide)
{
    r_page.position_count = (uint32_t)p_packed.positions.size();
    r_page.attribute_count = (uint32_t)p_packed.attributes.size();
    r_page.skin_count = (uint32_t)p_packed.skin.size();
    r_page.tri = _allocate(tri_alloc, ArenaKind::LOCAL_TRIANGLES, p_info.tri_count);
    r_page.position = _allocate(position_alloc, ArenaKind::POSITIONS, r_page.position_count);
    r_page.attribute = _allocate(attribute_alloc, ArenaKind::ATTRIBUTES, r_page.attribute_count);
    r_page.cluster = _allocate(cluster_alloc, ArenaKind::CLUSTERS, p_info.cluster_count);
    r_page.skin = _allocate(skin_alloc, ArenaKind::SKIN, r_page.skin_count);
    auto ok = [](uint32_t p_offset, uint32_t p_count) { return p_count == 0 || p_offset != RangeAllocator::INVALID; };
    const bool allocated = ok(r_page.tri, p_info.tri_count) && ok(r_page.position, r_page.position_count) && ok(r_page.attribute, r_page.attribute_count)
        && ok(r_page.cluster, p_info.cluster_count) && ok(r_page.skin, r_page.skin_count) && (!p_wide || _ensure_tri_slots(r_page.tri + p_info.tri_count));
    if (!allocated) _free_page(r_page, p_info);
    return allocated;
}

void GeometryPool::_free_page(StreamPage& r_page, const LMeshPage& p_info)
{
    tri_alloc.free(r_page.tri, p_info.tri_count);
    position_alloc.free(r_page.position, r_page.position_count);
    attribute_alloc.free(r_page.attribute, r_page.attribute_count);
    cluster_alloc.free(r_page.cluster, p_info.cluster_count);
    skin_alloc.free(r_page.skin, r_page.skin_count);
    r_page.tri = RangeAllocator::INVALID;
    r_page.position = RangeAllocator::INVALID;
    r_page.attribute = RangeAllocator::INVALID;
    r_page.cluster = RangeAllocator::INVALID;
    r_page.skin = RangeAllocator::INVALID;
    r_page.position_count = 0;
    r_page.attribute_count = 0;
    r_page.skin_count = 0;
}

void GeometryPool::_retire_page(StreamPage& r_page, const LMeshPage& p_info)
{
    _retired_pages.push_back({ frame_number, r_page.tri, p_info.tri_count, r_page.position, r_page.position_count, r_page.attribute, r_page.attribute_count, r_page.cluster, p_info.cluster_count, r_page.skin, r_page.skin_count });
    r_page.tri = RangeAllocator::INVALID;
    r_page.position = RangeAllocator::INVALID;
    r_page.attribute = RangeAllocator::INVALID;
    r_page.cluster = RangeAllocator::INVALID;
    r_page.skin = RangeAllocator::INVALID;
    r_page.position_count = 0;
    r_page.attribute_count = 0;
    r_page.skin_count = 0;
}

bool GeometryPool::_page_install(uint32_t p_mesh, uint32_t p_page, const LMeshPageData& p_data, const LMeshPackedPage& p_packed)
{
    StreamMesh& s = streams[p_mesh];
    StreamPage& sp = s.pages[p_page];
    const LMeshData& tables = *s.source->tables;
    const LMeshPage& info = tables.pages[p_page];
    const bool wide = has_flag((MeshFlags)s.source->flags, MeshFlags::WIDE_SLOTS);
    const bool skinned = has_flag((MeshFlags)s.source->flags, MeshFlags::SKINNED);
    const MeshRanges& r = mesh_ranges[p_mesh];
    const uint32_t group_count = s.page_groups_first[p_page + 1] - s.page_groups_first[p_page];
    const uint32_t leaf_count = s.page_leaves_first[p_page + 1] - s.page_leaves_first[p_page];

    const VkDeviceSize stream_words = (VkDeviceSize)p_packed.positions.size() + p_packed.attributes.size() + p_packed.skin.size();
    const VkDeviceSize need = (VkDeviceSize)info.tri_count * sizeof(uint32_t) * (wide ? 2u : 1u) + stream_words * sizeof(uint32_t)
        + (VkDeviceSize)info.cluster_count * (sizeof(Cluster) + sizeof(LMeshClusterStreams) + 5 * sizeof(uint32_t)) + (VkDeviceSize)(group_count + leaf_count + 1) * (sizeof(uint32_t) + 16) + 10 * 16;
    if (_staging_free() < need) return false;
    if (!_allocate_page(sp, info, p_packed, wide)) return false;

    _stage(ArenaKind::LOCAL_TRIANGLES, sp.tri, p_data.tris.data(), p_data.tris.size() * sizeof(uint32_t));
    if (wide) _stage(ArenaKind::TRI_SLOTS, sp.tri, p_data.tri_slots.data(), p_data.tri_slots.size() * sizeof(uint32_t));
    _stage(ArenaKind::POSITIONS, sp.position, p_packed.positions.data(), p_packed.positions.size() * sizeof(uint32_t));
    _stage(ArenaKind::ATTRIBUTES, sp.attribute, p_packed.attributes.data(), p_packed.attributes.size() * sizeof(uint32_t));
    if (skinned) _stage(ArenaKind::SKIN, sp.skin, p_packed.skin.data(), p_packed.skin.size() * sizeof(uint32_t));

    std::vector<Cluster> clusters;
    std::vector<uint32_t> cslots, bounds;
    _page_clusters(tables, p_page, sp.tri, r.group, clusters, cslots, bounds);
    std::vector<LMeshClusterStreams> cluster_streams = p_packed.clusters;
    _rebase_streams(cluster_streams, sp.position, sp.attribute, sp.skin);
    _stage(ArenaKind::CLUSTERS, sp.cluster, clusters.data(), clusters.size() * sizeof(Cluster));
    _stage(ArenaKind::CLUSTER_STREAMS, sp.cluster, cluster_streams.data(), cluster_streams.size() * sizeof(LMeshClusterStreams));
    _stage(ArenaKind::CLUSTER_CONES, sp.cluster, p_packed.cones.data(), p_packed.cones.size() * sizeof(uint32_t));
    _stage(ArenaKind::CLUSTER_SLOTS, sp.cluster, cslots.data(), cslots.size() * sizeof(uint32_t));
    _stage(ArenaKind::CLUSTER_BOUNDS, sp.cluster, bounds.data(), bounds.size() * sizeof(uint32_t));
    for (uint32_t k = s.page_leaves_first[p_page]; k < s.page_leaves_first[p_page + 1]; k++) {
        const uint32_t leaf = s.page_leaves[k];
        const uint32_t first = sp.cluster + (tables.nodes[leaf].first - info.cluster_first);
        _stage_bytes(ArenaKind::NODES, (VkDeviceSize)(r.node + leaf) * sizeof(LMeshNode) + offsetof(LMeshNode, first), &first, sizeof(uint32_t));
    }

    const uint32_t resident = PAGE_STREAMED;
    _stage(ArenaKind::PAGE_RESIDENT, r.page + p_page, &resident, sizeof(uint32_t));
    const uint32_t refined = 1u;
    for (uint32_t k = s.page_groups_first[p_page]; k < s.page_groups_first[p_page + 1]; k++) {
        const uint32_t g = s.page_groups[k];
        if (s.group_missing[g] > 0 && --s.group_missing[g] == 0) _stage(ArenaKind::GROUP_REFINED, r.group + g, &refined, sizeof(uint32_t));
    }

    sp.state = PageState::RESIDENT;
    sp.last_used = frame_number;
    sp.list_index = (uint32_t)_resident_list.size();
    _resident_list.push_back(((uint64_t)p_mesh << 32) | p_page);
    s.resident_pages++;
    _stream_resident_pages++;
    _stream_resident_bytes += _page_gpu_bytes(info, wide, skinned);
    return true;
}

void GeometryPool::_page_evict(uint32_t p_mesh, uint32_t p_page)
{
    StreamMesh& s = streams[p_mesh];
    StreamPage& sp = s.pages[p_page];
    const LMeshPage& info = s.source->tables->pages[p_page];
    const bool wide = has_flag((MeshFlags)s.source->flags, MeshFlags::WIDE_SLOTS);
    const bool skinned = has_flag((MeshFlags)s.source->flags, MeshFlags::SKINNED);
    const MeshRanges& r = mesh_ranges[p_mesh];

    const uint32_t absent = PAGE_ABSENT;
    _stage(ArenaKind::PAGE_RESIDENT, r.page + p_page, &absent, sizeof(uint32_t));
    const uint32_t coarse = 0u;
    for (uint32_t k = s.page_groups_first[p_page]; k < s.page_groups_first[p_page + 1]; k++) {
        const uint32_t g = s.page_groups[k];
        if (s.group_missing[g]++ == 0) _stage(ArenaKind::GROUP_REFINED, r.group + g, &coarse, sizeof(uint32_t));
    }

    _retire_page(sp, info);
    const uint64_t moved = _resident_list.back();
    _resident_list[sp.list_index] = moved;
    streams[(uint32_t)(moved >> 32)].pages[(uint32_t)moved].list_index = sp.list_index;
    _resident_list.pop_back();

    sp.list_index = UINT32_MAX;
    sp.state = PageState::ABSENT;
    s.resident_pages--;
    _stream_resident_pages -= std::min(_stream_resident_pages, 1u);
    _stream_resident_bytes -= std::min(_stream_resident_bytes, _page_gpu_bytes(info, wide, skinned));
    stream_stats.evictions++;
}

bool GeometryPool::_evict_for(uint64_t p_bytes, uint32_t& r_evictions, std::vector<uint64_t>& r_candidates, size_t& r_cursor, bool& r_built)
{
    if (!r_built) {
        r_built = true;
        r_candidates.clear();
        r_cursor = 0;
        if (frame_number < _evict_scan_after) return false;
        std::vector<std::pair<uint64_t, uint64_t>> scored;
        for (uint64_t key : _resident_list) {
            const StreamMesh& s = streams[(uint32_t)(key >> 32)];
            const StreamPage& sp = s.pages[(uint32_t)key];
            if (sp.last_used + STREAM_EVICT_AGE_FRAMES >= frame_number || s.required_until >= frame_number) continue;
            scored.push_back({ (sp.last_used << 24) | (0xFFFFFFull - ((uint32_t)key & 0xFFFFFFu)), key });
        }
        const size_t keep = std::min<size_t>(scored.size(), STREAM_MAX_EVICT_PER_FRAME);
        std::partial_sort(scored.begin(), scored.begin() + keep, scored.end());
        r_candidates.clear();
        for (size_t i = 0; i < keep; i++) r_candidates.push_back(scored[i].second);
        if (r_candidates.empty()) _evict_scan_after = frame_number + STREAM_EVICT_RESCAN_FRAMES;
    }
    while (_stream_resident_bytes + _stream_loading_bytes + p_bytes > stream_budget) {
        if (r_cursor >= r_candidates.size() || r_evictions >= STREAM_MAX_EVICT_PER_FRAME) return false;
        if (_staging_free() < 64 * 1024) return false;
        const uint64_t key = r_candidates[r_cursor++];
        const uint32_t mesh = (uint32_t)(key >> 32);
        const uint32_t page = (uint32_t)key;
        if (mesh >= streams.size() || !streams[mesh].streamed || page >= streams[mesh].pages.size() || streams[mesh].pages[page].state != PageState::RESIDENT || streams[mesh].pages[page].list_index == UINT32_MAX) continue;
        _page_evict(mesh, page);
        r_evictions++;
    }
    return true;
}

void GeometryPool::_stream_read_requests()
{
    _wanted.clear();
    stream_stats.requests = 0;
    if (current_slot < request_written.size() && request_written[current_slot]) {
        request_written[current_slot] = 0;
        Buffer& rb = request_readback[current_slot];
        dd->buffer_invalidate(rb, 0, _request_buffer_bytes());
        const uint32_t* d = reinterpret_cast<const uint32_t*>(rb.mapped);
        if (d) {
            const uint32_t count = std::min(d[0], STREAM_REQUEST_CAPACITY);
            stream_stats.requests = d[0];
            for (uint32_t i = 0; i < count; i++) {
                const uint32_t key = d[2 + 2 * i];
                const uint32_t gp = key & ~STREAM_REQUEST_RESIDENT_BIT;
                if (gp >= page_owner.size()) continue;
                const uint32_t mesh = page_owner[gp];
                if (mesh >= streams.size() || !streams[mesh].streamed) continue;
                StreamMesh& s = streams[mesh];
                const uint32_t local = gp - mesh_ranges[mesh].page;
                if (local >= s.pages.size()) continue;
                StreamPage& sp = s.pages[local];
                sp.last_used = frame_number;
                if ((key & STREAM_REQUEST_RESIDENT_BIT) == 0 && sp.state == PageState::ABSENT) {
                    float priority;
                    std::memcpy(&priority, &d[3 + 2 * i], sizeof(float));
                    _wanted.push_back({ mesh, local, priority });
                }
            }
        }
    }

    for (size_t i = 0; i < _required.size();) {
        const uint32_t mesh = _required[i];
        if (mesh >= streams.size() || !streams[mesh].streamed || streams[mesh].required_until < frame_number) {
            if (mesh < streams.size()) streams[mesh].required_until = 0;
            _required[i] = _required.back();
            _required.pop_back();
            continue;
        }
        StreamMesh& s = streams[mesh];
        for (uint32_t p = 0; p < (uint32_t)s.pages.size(); p++) {
            s.pages[p].last_used = frame_number;
            if (s.pages[p].state == PageState::ABSENT) _wanted.push_back({ mesh, p, FLT_MAX });
        }
        i++;
    }
}

void GeometryPool::_stream_take_results()
{
    reader.drain(_results);
    size_t keep = 0;
    bool stalled = false;
    for (size_t i = 0; i < _results.size(); i++) {
        MeshPageReader::Result& res = _results[i];
        const bool live = res.mesh < streams.size() && streams[res.mesh].streamed && streams[res.mesh].generation == res.generation
            && res.page < streams[res.mesh].pages.size() && streams[res.mesh].pages[res.page].state == PageState::LOADING;
        if (!live) {
            _stream_loading_bytes -= std::min<uint64_t>(_stream_loading_bytes, res.bytes);
            continue;
        }
        StreamMesh& s = streams[res.mesh];
        StreamPage& sp = s.pages[res.page];
        if (res.error) {
            log_write("GeometryPool: %s: page %u: %s", s.source->path.string().c_str(), res.page, res.error);
            sp.state = PageState::FAILED;
            s.failed_pages++;
            _stream_failed_pages++;
            s.loading_pages--;
            _stream_loading_bytes -= std::min<uint64_t>(_stream_loading_bytes, res.bytes);
            continue;
        }
        if (!stalled && _page_install(res.mesh, res.page, res.data, res.packed)) {
            s.loading_pages--;
            _stream_loading_bytes -= std::min<uint64_t>(_stream_loading_bytes, res.bytes);
            stream_stats.loads++;
            continue;
        }
        stalled = true;
        if (keep != i) _results[keep] = std::move(res);
        keep++;
    }
    _results.resize(keep);
}

void GeometryPool::_stream_issue_loads()
{
    if (_wanted.empty()) return;
    std::sort(_wanted.begin(), _wanted.end(), [](const Wanted& a, const Wanted& b) { return a.priority > b.priority; });

    std::vector<MeshPageReader::Request> batch;
    std::vector<uint64_t> candidates;
    size_t cursor = 0;
    bool built = false;
    uint32_t evictions = 0;
    const uint32_t room = STREAM_MAX_IN_FLIGHT > reader.in_flight ? STREAM_MAX_IN_FLIGHT - reader.in_flight : 0u;
    for (const Wanted& w : _wanted) {
        if (batch.size() >= room) break;
        StreamMesh& s = streams[w.mesh];
        StreamPage& sp = s.pages[w.page];
        if (sp.state != PageState::ABSENT) continue;
        const LMeshPage& info = s.source->tables->pages[w.page];
        const uint64_t bytes = _page_gpu_bytes(info, has_flag((MeshFlags)s.source->flags, MeshFlags::WIDE_SLOTS), has_flag((MeshFlags)s.source->flags, MeshFlags::SKINNED));
        if (_stream_resident_bytes + _stream_loading_bytes + bytes > stream_budget && !_evict_for(bytes, evictions, candidates, cursor, built)) {
            if (!s.blocked) _blocked.push_back(w.mesh);
            s.blocked = true;
            break;
        }
        sp.state = PageState::LOADING;
        s.loading_pages++;
        _stream_loading_bytes += bytes;
        batch.push_back({ w.mesh, s.generation, w.page, (uint32_t)bytes, s.source });
    }
    reader.submit(batch);
}

void GeometryPool::_stream_tick()
{
    if (current_slot < staging.size()) {
        staging[current_slot].used = 0;
        staging[current_slot].copies.clear();
    }
    stream_stats.loads = 0;
    stream_stats.evictions = 0;
    stream_stats.uploaded_bytes = 0;
    for (uint32_t id : _blocked) {
        if (id < streams.size()) streams[id].blocked = false;
    }
    _blocked.clear();
    _deferred_growth = true;

    _stream_read_requests();
    _stream_take_results();
    _stream_issue_loads();
    if (_stream_resident_bytes + _stream_loading_bytes > stream_budget) {
        std::vector<uint64_t> candidates;
        size_t cursor = 0;
        bool built = false;
        uint32_t evictions = 0;
        _evict_for(0, evictions, candidates, cursor, built);
    }

    _deferred_growth = false;

    stream_stats.in_flight = reader.in_flight;
    stream_stats.resident_bytes = _stream_resident_bytes;
    stream_stats.budget_bytes = stream_budget;
    stream_stats.resident_pages = _stream_resident_pages;
    stream_stats.streamed_pages = _stream_total_pages;
    stream_stats.failed_pages = _stream_failed_pages;
}

void GeometryPool::require(uint32_t p_id, uint32_t p_frames)
{
    if (p_id >= streams.size() || !streams[p_id].streamed) return;
    StreamMesh& s = streams[p_id];
    if (s.required_until < frame_number) _required.push_back(p_id);
    s.required_until = std::max<uint64_t>(s.required_until, frame_number + p_frames);
}

bool GeometryPool::fully_resident(uint32_t p_id) const
{
    if (!get(p_id)) return false;
    if (p_id >= streams.size() || !streams[p_id].streamed) return true;
    const StreamMesh& s = streams[p_id];
    return s.resident_pages + s.failed_pages >= s.pages.size();
}

bool GeometryPool::settled(uint32_t p_id) const
{
    if (fully_resident(p_id)) return true;
    if (p_id >= streams.size()) return true;
    const StreamMesh& s = streams[p_id];
    return s.loading_pages == 0 && s.blocked;
}

void GeometryPool::posed_box(uint32_t p_id, const mat4* p_skin, uint32_t p_count, vec3& r_min, vec3& r_max) const
{
    const LMesh* mesh = get(p_id);
    r_min = vec3(0.0f);
    r_max = vec3(0.0f);
    if (!mesh) return;
    r_min = mesh->pos_min;
    r_max = mesh->pos_min + mesh->pos_extent;
    const std::vector<JointBox>* boxes = joint_boxes(p_id);
    if (!boxes || p_count == 0) return;

    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (uint32_t j = 0; j < p_count && j < boxes->size(); j++) {
        const JointBox& b = (*boxes)[j];
        if (b.min.x > b.max.x) continue;
        const mat4& m = p_skin[j];
        const vec3 half = (b.max - b.min) * 0.5f;
        const vec3 center = vec3(m * vec4(b.min + half, 1.0f));
        vec3 extent(0.0f);
        for (int r = 0; r < 3; r++) extent[r] = std::abs(m[0][r]) * half.x + std::abs(m[1][r]) * half.y + std::abs(m[2][r]) * half.z;
        lo = glm::min(lo, center - extent);
        hi = glm::max(hi, center + extent);
    }
    if (lo.x > hi.x) return;
    r_min = lo;
    r_max = hi;
}

}
