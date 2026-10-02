#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/base/utils.h>
#include <algorithm>
#include <cstring>
#include <fstream>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

/***************/
/**** SETUP ****/
/***************/

Error GeometryPool::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count ? p_frame_count : 1;

    _arena_setup(ARENA_VERTICES, sizeof(Vertex), 1u << 20, MAX_VERTS, 0, "geo_vertices");
    _arena_setup(ARENA_INDICES, 3 * sizeof(uint32_t), 1u << 20, MAX_TRIS, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "geo_indices");
    _arena_setup(ARENA_TRI_SLOTS, sizeof(uint32_t), 1u << 20, MAX_TRIS, 0, "geo_tri_slots");
    _arena_setup(ARENA_SLOT_TABLE, sizeof(uint32_t), 16u << 10, MAX_SLOT_TABLE, 0, "geo_slot_table");
    _arena_setup(ARENA_CLUSTERS, sizeof(Cluster), 64u << 10, MAX_CLUSTERS, 0, "geo_clusters");
    _arena_setup(ARENA_GROUPS, sizeof(ClusterGroup), 16u << 10, MAX_GROUPS, 0, "geo_groups");
    _arena_setup(ARENA_SKIN, sizeof(SkinVertex), 64u << 10, MAX_SKIN_VERTS, 0, "geo_skin");
    _arena_setup(ARENA_BVH_NODES, sizeof(BVHNode), 64u << 10, MAX_BVH_NODES, 0, "geo_bvh_nodes");
    _arena_setup(ARENA_MESHES, sizeof(LMesh), 1u << 10, MAX_MESHES, 0, "geo_meshes");

    address_buffers.resize(frame_count);
    for (Buffer& b : address_buffers) {
        b = dd->buffer_create({ .size = sizeof(GeometryAddresses), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "geo_addresses" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "GeometryPool: address buffer allocation failed.");
        const GeometryAddresses zero{};
        dd->buffer_update(b, &zero, sizeof(zero));
        dd->buffer_flush(b, 0, sizeof(zero));
    }
    return OK;
}

void GeometryPool::shutdown()
{
    free();
    for (Buffer& b : address_buffers) dd->buffer_free(b);
    address_buffers.clear();
}

Error GeometryPool::allocate()
{
    if (allocated) return Error::OK;
    clear();
    allocated = true;
    return Error::OK;
}

void GeometryPool::free()
{
    for (Arena& a : arenas) {
        dd->buffer_free(a.buffer);
        a.buffer = {};
        a.capacity = 0;
    }
    for (RetiredBuffer& r : retired_buffers) dd->buffer_free(r.buffer);
    retired_buffers.clear();
    allocated = false;
    clear();
}

void GeometryPool::clear()
{
    meshes.clear();
    mesh_ranges.clear();
    mesh_guids.clear();
    free_meshes.clear();
    by_guid.clear();
    retired_meshes.clear();

    vertex_alloc.reset(arenas[ARENA_VERTICES].capacity);
    tri_alloc.reset(arenas[ARENA_INDICES].capacity);
    slot_table_alloc.reset(arenas[ARENA_SLOT_TABLE].capacity);
    cluster_alloc.reset(arenas[ARENA_CLUSTERS].capacity);
    group_alloc.reset(arenas[ARENA_GROUPS].capacity);
    skin_alloc.reset(arenas[ARENA_SKIN].capacity);
    bvh_alloc.reset(arenas[ARENA_BVH_NODES].capacity);
    cluster_extent = 0;
}

VkDeviceSize GeometryPool::resident_bytes() const
{
    VkDeviceSize total = 0;
    for (const Arena& a : arenas) total += a.buffer.capacity;
    for (const RetiredBuffer& r : retired_buffers) total += r.buffer.capacity;
    return total;
}

/****************/
/**** ARENAS ****/
/****************/

void GeometryPool::_arena_setup(ArenaKind p_kind, uint32_t p_stride, uint32_t p_initial, uint32_t p_max, VkBufferUsageFlags p_usage, const char* p_name)
{
    Arena& a = arenas[p_kind];
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
    const Arena& a = arenas[p_kind];
    if (p_min > a.max) return 0;
    const uint64_t doubled = a.capacity ? (uint64_t)a.capacity * 2 : a.initial;
    return (uint32_t)std::min<uint64_t>(std::max(doubled, p_min), a.max);
}

Error GeometryPool::_arena_grow(ArenaKind p_kind, uint32_t p_capacity, uint32_t p_live)
{
    using enum Error;
    Arena& a = arenas[p_kind];
    if (p_capacity <= a.capacity) return OK;

    Buffer old = a.buffer;
    const uint32_t old_capacity = a.capacity;
    if (_arena_create(a, p_capacity) != OK) {
        a.buffer = old;
        a.capacity = old_capacity;
        return FAILED;
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
    log_write("GeometryPool: %s -> %u elements (%s)", a.name, p_capacity, fmt_bytes(a.buffer.capacity));
    return OK;
}

uint32_t GeometryPool::_allocate(RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count)
{
    if (p_count == 0) return RangeAllocator::INVALID;

    const uint32_t offset = r_alloc.allocate(p_count);
    if (offset != RangeAllocator::INVALID) return offset;

    const uint32_t capacity = _grow_target(p_kind, (uint64_t)r_alloc.capacity + p_count - r_alloc.tail_free());
    if (capacity == 0) return RangeAllocator::INVALID;

    const uint32_t live = r_alloc.extent();
    if (p_kind == ARENA_INDICES && _arena_grow(ARENA_TRI_SLOTS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (_arena_grow(p_kind, capacity, live) != Error::OK) return RangeAllocator::INVALID;

    r_alloc.grow(capacity);
    return r_alloc.allocate(p_count);
}

void GeometryPool::_release(const MeshRanges& p_r)
{
    vertex_alloc.free(p_r.vertex, p_r.vertex_count);
    tri_alloc.free(p_r.tri, p_r.tri_count);
    slot_table_alloc.free(p_r.slot_table, p_r.slot_table_count);
    cluster_alloc.free(p_r.cluster, p_r.cluster_count);
    group_alloc.free(p_r.group, p_r.group_count);
    skin_alloc.free(p_r.skin, p_r.skin_count);
    bvh_alloc.free(p_r.bvh, p_r.bvh_count);
}

void GeometryPool::_retire_buffer(Buffer& r_buffer)
{
    if (!r_buffer.buffer) return;
    retired_buffers.push_back({ frame_number, r_buffer });
    r_buffer = {};
}

void GeometryPool::_collect(uint64_t p_frame_number)
{
    for (size_t i = 0; i < retired_meshes.size();) {
        if (retired_meshes[i].frame + frame_count > p_frame_number) { i++; continue; }
        _release(retired_meshes[i].ranges);
        free_meshes.push_back(retired_meshes[i].id);
        retired_meshes[i] = retired_meshes.back();
        retired_meshes.pop_back();
    }
    for (size_t i = 0; i < retired_buffers.size();) {
        if (retired_buffers[i].frame + frame_count > p_frame_number) { i++; continue; }
        dd->buffer_free(retired_buffers[i].buffer);
        retired_buffers[i] = retired_buffers.back();
        retired_buffers.pop_back();
    }
}

/****************/
/**** MESHES ****/
/****************/

uint32_t GeometryPool::load(Guid p_guid, const std::filesystem::path& p_path)
{
    if (!allocated) return INVALID_MESH;
    if (auto it = by_guid.find(p_guid); it != by_guid.end()) return it->second;

    const std::string name = p_path.string();
    auto fail = [&](const char* p_why) { log_write("GeometryPool: %s: %s", name.c_str(), p_why); return INVALID_MESH; };

    std::ifstream f(p_path, std::ios::binary | std::ios::ate);
    if (!f) return fail("cannot open");
    const std::streamsize file_size = f.tellg();
    f.seekg(0);
    if (file_size < (std::streamsize)(sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader))) return fail("too small");

    std::vector<uint8_t> bytes((size_t)file_size);
    f.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!f) return fail("read failed");

    LAssetHeader ah{};
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    if (ah.magic != BCON_MAGIC || ah.version != LASSET_VERSION || ah.type != AssetType::MESH) return fail("bad asset header");

    LMeshPayloadHeader ph{};
    std::memcpy(&ph, bytes.data() + sizeof(ah), sizeof(ph));
    if (ah.payload_size < sizeof(ph)) return fail("payload_size underflow");

    const bool skinned = (ph.flags & MESH_FLAG_SKINNED) != 0;
    const size_t vtx_bytes = (size_t)ph.vertex_count * sizeof(Vertex);
    const size_t idx_bytes = (size_t)ph.index_count * sizeof(uint32_t);
    const size_t tri_bytes = (size_t)ph.tri_count * sizeof(uint32_t);
    const size_t slot_bytes = (size_t)ph.slot_table_count * sizeof(uint32_t);
    const size_t clus_bytes = (size_t)ph.cluster_count * sizeof(Cluster);
    const size_t group_bytes = (size_t)ph.group_count * sizeof(ClusterGroup);
    const size_t skin_bytes = skinned ? (size_t)ph.vertex_count * sizeof(SkinVertex) : 0;
    const size_t bvhn_bytes = (size_t)ph.bvh_node_count * sizeof(BVHNode);

    const size_t payload_off = sizeof(ah) + sizeof(ph);
    const size_t blocks_size = (size_t)ah.payload_size - sizeof(ph);
    if (vtx_bytes + idx_bytes + tri_bytes + slot_bytes + clus_bytes + group_bytes + skin_bytes + bvhn_bytes != blocks_size || payload_off + blocks_size > bytes.size()) return fail("bad/truncated payload");
    if (ph.vertex_count == 0 || ph.index_count == 0 || ph.index_count % 3 != 0 || ph.tri_count != ph.index_count / 3) return fail("bad vertex/index counts");

    uint32_t id = INVALID_MESH;
    if (!free_meshes.empty()) id = free_meshes.back();
    else if (meshes.size() < MAX_MESHES) id = (uint32_t)meshes.size();
    if (id == INVALID_MESH) return fail("mesh table full");
    if (id >= arenas[ARENA_MESHES].capacity) {
        const uint32_t capacity = _grow_target(ARENA_MESHES, (uint64_t)id + 1);
        if (capacity == 0 || _arena_grow(ARENA_MESHES, capacity, (uint32_t)meshes.size()) != Error::OK) return fail("mesh table growth failed");
    }

    MeshRanges r;
    r.vertex_count = ph.vertex_count;
    r.tri_count = ph.tri_count;
    r.slot_table_count = ph.slot_table_count;
    r.cluster_count = ph.cluster_count;
    r.group_count = ph.group_count;
    r.skin_count = skinned ? ph.vertex_count : 0;
    r.bvh_count = ph.bvh_node_count;

    auto reserve = [&](uint32_t& r_offset, RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count) {
        if (p_count == 0) return true;
        r_offset = _allocate(r_alloc, p_kind, p_count);
        return r_offset != RangeAllocator::INVALID;
    };
    const bool reserved =
        reserve(r.vertex, vertex_alloc, ARENA_VERTICES, r.vertex_count) &&
        reserve(r.tri, tri_alloc, ARENA_INDICES, r.tri_count) &&
        reserve(r.slot_table, slot_table_alloc, ARENA_SLOT_TABLE, r.slot_table_count) &&
        reserve(r.cluster, cluster_alloc, ARENA_CLUSTERS, r.cluster_count) &&
        reserve(r.group, group_alloc, ARENA_GROUPS, r.group_count) &&
        reserve(r.skin, skin_alloc, ARENA_SKIN, r.skin_count) &&
        reserve(r.bvh, bvh_alloc, ARENA_BVH_NODES, r.bvh_count);
    if (!reserved) {
        _release(r);
        return fail("geometry arenas exhausted");
    }

    uint8_t* p = bytes.data() + payload_off;
    uint8_t* p_vtx = p; p += vtx_bytes;
    uint8_t* p_idx = p; p += idx_bytes;
    uint8_t* p_tri = p; p += tri_bytes;
    uint8_t* p_slot = p; p += slot_bytes;
    uint8_t* p_clus = p; p += clus_bytes;
    uint8_t* p_group = p; p += group_bytes;
    uint8_t* p_skin = p; p += skin_bytes;
    uint8_t* p_bvhn = p; p += bvhn_bytes;

    const char* bad = nullptr;
    const uint32_t index_base = r.tri * 3;

    uint32_t* idx = reinterpret_cast<uint32_t*>(p_idx);
    for (uint32_t i = 0; i < ph.index_count && !bad; i++) {
        if (idx[i] >= ph.vertex_count) bad = "index out of range";
        idx[i] += r.vertex;
    }

    const uint32_t* tri = reinterpret_cast<const uint32_t*>(p_tri);
    for (uint32_t i = 0; i < ph.tri_count && !bad; i++) {
        if (tri[i] >= ph.slot_table_count) bad = "triangle slot out of range";
    }

    Cluster* clus = reinterpret_cast<Cluster*>(p_clus);
    for (uint32_t i = 0; i < ph.cluster_count && !bad; i++) {
        Cluster& c = clus[i];
        if ((uint64_t)c.index_base + c.index_count > ph.index_count) bad = "cluster index range out of bounds";
        else if (c.self_group != CLUSTER_GROUP_NONE && c.self_group >= ph.group_count) bad = "cluster self_group out of range";
        else if (c.parent_group != CLUSTER_GROUP_NONE && c.parent_group >= ph.group_count) bad = "cluster parent_group out of range";
        c.index_base += index_base;
        if (c.self_group != CLUSTER_GROUP_NONE) c.self_group += r.group;
        if (c.parent_group != CLUSTER_GROUP_NONE) c.parent_group += r.group;
    }

    BVHNode* bvhn = reinterpret_cast<BVHNode*>(p_bvhn);
    for (uint32_t i = 0; i < ph.bvh_node_count && !bad; i++) {
        BVHNode& n = bvhn[i];
        if (n.left & BVH_LEAF_BIT) {
            const uint64_t first = n.left & ~BVH_LEAF_BIT;
            if (first + n.right > ph.tri_count) bad = "bvh leaf range out of bounds";
            n.left = BVH_LEAF_BIT | (uint32_t)(first + r.tri);
        } else {
            if (n.left >= ph.bvh_node_count || n.right >= ph.bvh_node_count) bad = "bvh child out of range";
            n.left += r.bvh;
            n.right += r.bvh;
        }
    }

    if (bad) {
        _release(r);
        return fail(bad);
    }

    LMesh m{};
    m.vertex_base = r.vertex;
    m.vertex_count = ph.vertex_count;
    m.cluster_base = r.cluster_count ? r.cluster : 0;
    m.cluster_count = ph.cluster_count;
    m.slot_table_base = r.slot_table_count ? r.slot_table : 0;
    m.slot_table_count = ph.slot_table_count;
    m.skin_base = skinned ? r.skin : UINT32_MAX;
    m.bvh_node_base = r.bvh_count ? r.bvh : 0;
    m.bvh_node_count = ph.bvh_node_count;
    m.pos_min = ph.pos_min;
    m.pos_extent = ph.pos_extent;
    m.uv_min = ph.uv_min;
    m.uv_extent = ph.uv_extent;
    m.bounds_sphere = ph.bounds_sphere;

    drivers::DeviceDriverVulkan::BufferUpload uploads[ARENA_COUNT];
    uint32_t upload_count = 0;
    auto add = [&](ArenaKind p_kind, uint32_t p_offset, const void* p_data, size_t p_bytes) {
        if (p_bytes) uploads[upload_count++] = { &arenas[p_kind].buffer, p_data, (VkDeviceSize)p_bytes, (VkDeviceSize)p_offset * arenas[p_kind].stride };
    };
    add(ARENA_VERTICES, r.vertex, p_vtx, vtx_bytes);
    add(ARENA_INDICES, r.tri, p_idx, idx_bytes);
    add(ARENA_TRI_SLOTS, r.tri, p_tri, tri_bytes);
    add(ARENA_SLOT_TABLE, r.slot_table, p_slot, slot_bytes);
    add(ARENA_CLUSTERS, r.cluster, p_clus, clus_bytes);
    add(ARENA_GROUPS, r.group, p_group, group_bytes);
    add(ARENA_SKIN, r.skin, p_skin, skin_bytes);
    add(ARENA_BVH_NODES, r.bvh, p_bvhn, bvhn_bytes);
    add(ARENA_MESHES, id, &m, sizeof(LMesh));

    if (dd->buffer_upload_batch(uploads, upload_count) != Error::OK) {
        _release(r);
        return fail("upload failed");
    }

    if (id == meshes.size()) {
        meshes.push_back(m);
        mesh_ranges.push_back(r);
        mesh_guids.push_back(p_guid);
    } else {
        free_meshes.pop_back();
        meshes[id] = m;
        mesh_ranges[id] = r;
        mesh_guids[id] = p_guid;
    }
    by_guid.emplace(p_guid, id);

    log_write("GeometryPool: loaded %s verts=%u tris=%u clusters=%u groups=%u bvh_nodes=%u id=%u", name.c_str(), ph.vertex_count, ph.tri_count, ph.cluster_count, ph.group_count, ph.bvh_node_count, id);
    return id;
}

void GeometryPool::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    const uint32_t id = it->second;
    by_guid.erase(it);

    retired_meshes.push_back({ frame_number, id, mesh_ranges[id] });
    meshes[id] = LMesh{};
    mesh_ranges[id] = MeshRanges{};
    mesh_guids[id] = Guid{};
}

/***************/
/**** FRAME ****/
/***************/

void GeometryPool::begin_frame(uint64_t p_frame_number, uint32_t p_slot)
{
    frame_number = p_frame_number;
    _collect(p_frame_number);

    GeometryAddresses a{};
    a.vertices = arenas[ARENA_VERTICES].buffer.device_address;
    a.indices = arenas[ARENA_INDICES].buffer.device_address;
    a.tri_slots = arenas[ARENA_TRI_SLOTS].buffer.device_address;
    a.slot_table = arenas[ARENA_SLOT_TABLE].buffer.device_address;
    a.clusters = arenas[ARENA_CLUSTERS].buffer.device_address;
    a.groups = arenas[ARENA_GROUPS].buffer.device_address;
    a.skin_vertices = arenas[ARENA_SKIN].buffer.device_address;
    a.bvh_nodes = arenas[ARENA_BVH_NODES].buffer.device_address;
    a.meshes = arenas[ARENA_MESHES].buffer.device_address;

    Buffer& b = address_buffers[p_slot];
    dd->buffer_update(b, &a, sizeof(a));
    dd->buffer_flush(b, 0, sizeof(a));

    cluster_extent = cluster_alloc.extent();
}

}