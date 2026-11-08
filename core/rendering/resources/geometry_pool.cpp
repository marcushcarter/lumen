#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/base/strings.h>
#include <algorithm>
#include <cstring>
#include <fstream>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error GeometryPool::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count ? p_frame_count : 1;

    _arena_setup(ArenaKind::TRI_SLOTS, sizeof(uint32_t), 1u << 20, MAX_TRIS, 0, "geo_tri_slots");
    _arena_setup(ArenaKind::CLUSTERS, sizeof(Cluster), 64u << 10, MAX_CLUSTERS, 0, "geo_clusters");
    _arena_setup(ArenaKind::GROUPS, sizeof(ClusterGroup), 16u << 10, MAX_GROUPS, 0, "geo_groups");
    _arena_setup(ArenaKind::SKIN, sizeof(SkinVertex), 64u << 10, MAX_SKIN_VERTS, 0, "geo_skin");
    _arena_setup(ArenaKind::MESHES, sizeof(LMesh), 1u << 10, MAX_MESHES, 0, "geo_meshes");
    _arena_setup(ArenaKind::CLUSTER_VERTEX_BASE, sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_vertex_base");
    _arena_setup(ArenaKind::LOCAL_TRIANGLES, sizeof(uint32_t), 1u << 20, MAX_TRIS, 0, "geo_local_triangles");
    _arena_setup(ArenaKind::CLUSTER_VERTICES, sizeof(ClusterVertex), 1u << 20, MAX_CLUSTER_VERTS, 0, "geo_cluster_vertices");
    _arena_setup(ArenaKind::CLUSTER_SLOTS, sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_slots");
    _arena_setup(ArenaKind::CLUSTER_BOUNDS, 3 * sizeof(uint32_t), 64u << 10, MAX_CLUSTERS, 0, "geo_cluster_bounds");
    _arena_setup(ArenaKind::NODES, sizeof(LMeshNode), 64u << 10, MAX_NODES, 0, "geo_nodes");

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

void GeometryPool::begin_frame(uint64_t p_frame_number, uint32_t p_slot, VkDeviceAddress p_slot_table, VkDeviceAddress p_materials)
{
    frame_number = p_frame_number;
    _collect(p_frame_number);

    GeometryAddresses a{};
    a.tri_slots = _arena(ArenaKind::TRI_SLOTS).buffer.device_address;
    a.slot_table = p_slot_table;
    a.clusters = _arena(ArenaKind::CLUSTERS).buffer.device_address;
    a.groups = _arena(ArenaKind::GROUPS).buffer.device_address;
    a.skin_vertices = _arena(ArenaKind::SKIN).buffer.device_address;
    a.meshes = _arena(ArenaKind::MESHES).buffer.device_address;
    a.cluster_vertex_base = _arena(ArenaKind::CLUSTER_VERTEX_BASE).buffer.device_address;
    a.local_triangles = _arena(ArenaKind::LOCAL_TRIANGLES).buffer.device_address;
    a.cluster_vertices = _arena(ArenaKind::CLUSTER_VERTICES).buffer.device_address;
    a.materials = p_materials;
    a.cluster_slots = _arena(ArenaKind::CLUSTER_SLOTS).buffer.device_address;
    a.cluster_bounds = _arena(ArenaKind::CLUSTER_BOUNDS).buffer.device_address;
    a.nodes = _arena(ArenaKind::NODES).buffer.device_address;

    Buffer& b = address_buffers[p_slot];
    dd->buffer_update(b, &a, sizeof(a));
    dd->buffer_flush(b, 0, sizeof(a));

    cluster_extent = cluster_alloc.extent();
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
    if (p_kind == ArenaKind::LOCAL_TRIANGLES && _arena_grow(ArenaKind::TRI_SLOTS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_VERTEX_BASE, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_SLOTS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (p_kind == ArenaKind::CLUSTERS && _arena_grow(ArenaKind::CLUSTER_BOUNDS, capacity, live) != Error::OK) return RangeAllocator::INVALID;
    if (_arena_grow(p_kind, capacity, live) != Error::OK) return RangeAllocator::INVALID;

    r_alloc.grow(capacity);
    return r_alloc.allocate(p_count);
}

void GeometryPool::_release(const MeshRanges& p_r)
{
    vertex_alloc.free(p_r.vertex, p_r.vertex_count);
    tri_alloc.free(p_r.tri, p_r.tri_count);
    cluster_alloc.free(p_r.cluster, p_r.cluster_count);
    group_alloc.free(p_r.group, p_r.group_count);
    skin_alloc.free(p_r.skin, p_r.skin_count);
    node_alloc.free(p_r.node, p_r.node_count);
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
    version++;
    meshes.clear();
    mesh_ranges.clear();
    mesh_guids.clear();
    mesh_joint_boxes.clear();
    mesh_rigs.clear();
    free_meshes.clear();
    by_guid.clear();
    _retired_meshes.clear();

    vertex_alloc.reset(_arena(ArenaKind::CLUSTER_VERTICES).capacity);
    tri_alloc.reset(_arena(ArenaKind::LOCAL_TRIANGLES).capacity);
    cluster_alloc.reset(_arena(ArenaKind::CLUSTERS).capacity);
    group_alloc.reset(_arena(ArenaKind::GROUPS).capacity);
    skin_alloc.reset(_arena(ArenaKind::SKIN).capacity);
    node_alloc.reset(_arena(ArenaKind::NODES).capacity);
    cluster_extent = 0;
    max_cluster_tris = 0;
    max_node_depth = 0;
}

uint32_t GeometryPool::load(Guid p_guid, const std::filesystem::path& p_path)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end()) return it->second;

    const std::string name = p_path.string();
    auto fail = [&](const char* p_why) { log_write("GeometryPool: %s: %s", name.c_str(), p_why); return INVALID_MESH; };

    LMeshData data;
    std::string error;
    if (!lmesh_load(p_path, data, error)) return fail(error.c_str());
    const LMeshPayloadHeader& h = data.header;
    const bool skinned = data.skinned();

    uint32_t id = INVALID_MESH;
    if (!free_meshes.empty()) id = free_meshes.back();
    else if (meshes.size() < MAX_MESHES) id = (uint32_t)meshes.size();
    if (id == INVALID_MESH) return fail("mesh table full");
    if (id >= _arena(ArenaKind::MESHES).capacity) {
        const uint32_t capacity = _grow_target(ArenaKind::MESHES, (uint64_t)id + 1);
        if (capacity == 0 || _arena_grow(ArenaKind::MESHES, capacity, (uint32_t)meshes.size()) != Error::OK) return fail("mesh table growth failed");
    }

    MeshRanges r;
    r.vertex_count = h.vertex_count;
    r.tri_count = h.tri_count;
    r.cluster_count = h.cluster_count;
    r.group_count = h.group_count;
    r.skin_count = skinned ? h.vertex_count : 0;
    r.node_count = h.node_count;

    auto reserve = [&](uint32_t& r_offset, RangeAllocator& r_alloc, ArenaKind p_kind, uint32_t p_count) {
        if (p_count == 0) return true;
        r_offset = _allocate(r_alloc, p_kind, p_count);
        return r_offset != RangeAllocator::INVALID;
    };
    const bool reserved =
        reserve(r.vertex, vertex_alloc, ArenaKind::CLUSTER_VERTICES, r.vertex_count) &&
        reserve(r.tri, tri_alloc, ArenaKind::LOCAL_TRIANGLES, r.tri_count) &&
        reserve(r.cluster, cluster_alloc, ArenaKind::CLUSTERS, r.cluster_count) &&
        reserve(r.group, group_alloc, ArenaKind::GROUPS, r.group_count) &&
        reserve(r.skin, skin_alloc, ArenaKind::SKIN, r.skin_count) &&
        reserve(r.node, node_alloc, ArenaKind::NODES, r.node_count);
    if (!reserved) {
        _release(r);
        return fail("geometry arenas exhausted");
    }

    auto rebase_group = [&](uint32_t p_group) { return p_group == CLUSTER_GROUP_NONE ? CLUSTER_GROUP_NONE : p_group + r.group; };
    std::vector<Cluster> clusters(h.cluster_count);
    std::vector<uint32_t> cluster_vbase(h.cluster_count);
    std::vector<uint32_t> cluster_slots(h.cluster_count);
    std::vector<uint32_t> cluster_bounds((size_t)h.cluster_count * 3);
    uint32_t mesh_max_tris = 0;
    for (const LMeshPage& page : data.pages) {
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
            const LMeshCluster& c = data.clusters[ci];
            clusters[ci] = Cluster{ (r.tri + page.tri_first + c.tri_offset) * 3u, c.tri_count * 3u, c.cull_sphere, rebase_group(c.self_group), rebase_group(c.parent_group) };
            cluster_vbase[ci] = r.vertex + page.vertex_first + c.vertex_offset;
            cluster_slots[ci] = c.slot;
            std::memcpy(&cluster_bounds[(size_t)ci * 3], c.bounds, sizeof(c.bounds));
            mesh_max_tris = std::max(mesh_max_tris, c.tri_count);
        }
    }

    std::vector<LMeshNode> nodes = data.nodes;
    std::vector<uint32_t> depth(nodes.size(), 1u);
    uint32_t mesh_depth = 1;
    for (uint32_t i = 0; i < (uint32_t)nodes.size(); i++) {
        LMeshNode& n = nodes[i];
        if (n.page == LMESH_NODE_INNER) {
            for (uint32_t k = n.first; k < n.first + n.count; k++) depth[k] = depth[i] + 1;
            n.first += r.node;
        } else {
            n.first += r.cluster;
        }
        mesh_depth = std::max(mesh_depth, depth[i]);
    }
    if (mesh_depth > MAX_NODE_DEPTH) {
        _release(r);
        return fail("cluster hierarchy is deeper than the traversal supports");
    }

    std::vector<JointBox> joint_boxes;
    if (skinned) {
        for (uint32_t v = 0; v < h.vertex_count; v++) {
            const SkinVertex& sv = data.skin[v];
            const vec3 pos = h.pos_min + vec3(data.vertices[v].position.position) / 65535.0f * h.pos_extent;
            for (int k = 0; k < 4; k++) {
                if (sv.weights[k] == 0) continue;
                const uint32_t j = sv.joints[k];
                if (j >= joint_boxes.size()) joint_boxes.resize(j + 1);
                joint_boxes[j].min = glm::min(joint_boxes[j].min, pos);
                joint_boxes[j].max = glm::max(joint_boxes[j].max, pos);
            }
        }
    }

    LMesh m{};
    m.cluster_base = r.cluster_count ? r.cluster : 0;
    m.cluster_count = h.cluster_count;
    m.slot_count = h.slot_count;
    m.skin_base = skinned ? r.skin - r.vertex : 0;
    m.node_base = r.node_count ? r.node : 0;
    m.node_count = h.node_count;
    m.pos_min = h.pos_min;
    m.pos_extent = h.pos_extent;
    m.uv_min = h.uv_min;
    m.uv_extent = h.uv_extent;
    m.bounds_sphere = h.bounds_sphere;

    drivers::DeviceDriverVulkan::BufferUpload uploads[ARENA_COUNT];
    uint32_t upload_count = 0;
    auto add = [&](ArenaKind p_kind, uint32_t p_offset, const void* p_data, size_t p_bytes) {
        if (p_bytes) uploads[upload_count++] = { &_arena(p_kind).buffer, p_data, (VkDeviceSize)p_bytes, (VkDeviceSize)p_offset * _arena(p_kind).stride };
    };
    add(ArenaKind::CLUSTER_VERTICES, r.vertex, data.vertices.data(), data.vertices.size() * sizeof(ClusterVertex));
    add(ArenaKind::LOCAL_TRIANGLES, r.tri, data.local_tris.data(), data.local_tris.size() * sizeof(uint32_t));
    add(ArenaKind::TRI_SLOTS, r.tri, data.tri_slots.data(), data.tri_slots.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTERS, r.cluster, clusters.data(), clusters.size() * sizeof(Cluster));
    add(ArenaKind::CLUSTER_VERTEX_BASE, r.cluster, cluster_vbase.data(), cluster_vbase.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTER_SLOTS, r.cluster, cluster_slots.data(), cluster_slots.size() * sizeof(uint32_t));
    add(ArenaKind::CLUSTER_BOUNDS, r.cluster, cluster_bounds.data(), cluster_bounds.size() * sizeof(uint32_t));
    add(ArenaKind::GROUPS, r.group, data.groups.data(), data.groups.size() * sizeof(ClusterGroup));
    add(ArenaKind::SKIN, r.skin, data.skin.data(), data.skin.size() * sizeof(SkinVertex));
    add(ArenaKind::NODES, r.node, nodes.data(), nodes.size() * sizeof(LMeshNode));
    add(ArenaKind::MESHES, id, &m, sizeof(LMesh));

    if (dd->buffer_upload_batch(uploads, upload_count) != Error::OK) {
        _release(r);
        return fail("upload failed");
    }

    if (id == meshes.size()) {
        meshes.push_back(m);
        mesh_ranges.push_back(r);
        mesh_guids.push_back(p_guid);
        mesh_joint_boxes.push_back(std::move(joint_boxes));
        mesh_rigs.push_back(std::move(data.rig));
    } else {
        free_meshes.pop_back();
        meshes[id] = m;
        mesh_ranges[id] = r;
        mesh_guids[id] = p_guid;
        mesh_joint_boxes[id] = std::move(joint_boxes);
        mesh_rigs[id] = std::move(data.rig);
    }
    by_guid.emplace(p_guid, id);
    max_cluster_tris = std::max(max_cluster_tris, mesh_max_tris);
    max_node_depth = std::max(max_node_depth, mesh_depth);
    version++;

    log_write("GeometryPool: loaded %s tris=%u cluster_verts=%u clusters=%u groups=%u pages=%u (%u root) nodes=%u depth=%u id=%u", name.c_str(), h.tri_count, h.vertex_count, h.cluster_count, h.group_count, h.page_count, h.root_page_count, h.node_count, mesh_depth, id);
    return id;
}

void GeometryPool::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    const uint32_t id = it->second;
    by_guid.erase(it);
    version++;

    _retired_meshes.push_back({ frame_number, id, mesh_ranges[id] });
    meshes[id] = LMesh{};
    mesh_ranges[id] = MeshRanges{};
    mesh_guids[id] = Guid{};
    mesh_joint_boxes[id].clear();
    mesh_rigs[id] = {};
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
