#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <fstream>

namespace lumen {
    
Error GeometryPool::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    using enum Error;
    dd = &r_dd;
    return Ok;
}

Error GeometryPool::allocate()
{
    using enum Error;

    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vertex_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_VERTS * sizeof(Vertex), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_vertices" });
    index_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_INDICES * sizeof(uint32_t), .usage = usage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_indices" });
    tri_slot_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_INDICES/3 * sizeof(uint32_t), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_tri_slots" });
    slot_table_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_SLOT_TABLE * sizeof(uint32_t), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_slot_table" });
    cluster_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_CLUSTERS * sizeof(Cluster), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_clusters" });
    skin_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_SKIN_VERTS * sizeof(SkinVertex), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_skin" });
    bvh_nodes_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_BVH_NODES * sizeof(BVHNode), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_bvh_nodes" });
    mesh_buffer = dd->buffer_create({ .size = (VkDeviceSize)MAX_MESHES * sizeof(LMesh), .usage = usage, .device_local = true, .pool = dd->buffer_geometry_pool, .name = "geo_meshes" });
    address_buffer = dd->buffer_create({ .size = (VkDeviceSize)sizeof(GeometryAddresses), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "geo_addresses" });

    GeometryAddresses a{};
    a.vertices = vertex_buffer.device_address;
    a.indices = index_buffer.device_address;
    a.tri_slots = tri_slot_buffer.device_address;
    a.slot_table = slot_table_buffer.device_address;
    a.clusters = cluster_buffer.device_address;
    a.skin_vertices = skin_buffer.device_address;
    a.bvh_nodes = bvh_nodes_buffer.device_address;
    a.meshes = mesh_buffer.device_address;
    dd->buffer_update(address_buffer, &a, sizeof(a));
    allocated = true;

    return Ok;
}

void GeometryPool::free()
{
    dd->buffer_free(vertex_buffer);
    dd->buffer_free(skin_buffer);
    dd->buffer_free(index_buffer);
    dd->buffer_free(tri_slot_buffer);
    dd->buffer_free(slot_table_buffer);
    dd->buffer_free(cluster_buffer);
    dd->buffer_free(mesh_buffer);
    dd->buffer_free(bvh_nodes_buffer);
    dd->buffer_free(address_buffer);
    allocated = false;
    clear();
}

uint32_t GeometryPool::load(Guid p_guid, const std::filesystem::path& p_path)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end()) return it->second;

    std::ifstream f(p_path, std::ios::binary | std::ios::ate);
    if (!f) { log_write("GeometryPool: cannot open %s", p_path.string().c_str()); return UINT32_MAX; }

    const std::streamsize file_size = f.tellg();
    f.seekg(0);
    if (file_size < (std::streamsize)(sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader))) {
        log_write("GeometryPool: %s too small", p_path.string().c_str());
        return UINT32_MAX;
    }

    std::vector<uint8_t> bytes((size_t)file_size);
    f.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!f) { log_write("GeometryPool: read failed %s", p_path.string().c_str()); return UINT32_MAX; }

    LAssetHeader ah{};
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    if (ah.magic != BCON_MAGIC || ah.version != LASSET_VERSION || ah.type != AssetType::Mesh) {
        log_write("GeometryPool: bad asset header %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    LMeshPayloadHeader ph{};
    std::memcpy(&ph, bytes.data() + sizeof(ah), sizeof(ph));
    if (ah.payload_size < sizeof(ph)) {
        log_write("GeometryPool: payload_size underflow %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    const bool skinned = (ph.flags & MESH_FLAG_SKINNED) != 0;

    const size_t vtx_bytes = (size_t)ph.vertex_count * sizeof(Vertex);
    const size_t idx_bytes = (size_t)ph.index_count * sizeof(uint32_t);
    const size_t tri_bytes = (size_t)ph.tri_count * sizeof(uint32_t);
    const size_t slot_bytes = (size_t)ph.slot_table_count * sizeof(uint32_t);
    const size_t clus_bytes = (size_t)ph.cluster_count * sizeof(Cluster);
    const size_t skin_bytes = skinned ? (size_t)ph.vertex_count * sizeof(SkinVertex) : 0;
    const size_t bvhn_bytes = (size_t)ph.bvh_node_count * sizeof(BVHNode);

    const size_t payload_off = sizeof(ah) + sizeof(ph);
    const size_t blocks_size = (size_t)ah.payload_size - sizeof(ph);
    if (ph.tri_count != ph.index_count/3 || vtx_bytes + idx_bytes + tri_bytes + slot_bytes + clus_bytes + skin_bytes + bvhn_bytes != blocks_size || payload_off + blocks_size > bytes.size()) {
        log_write("GeometryPool: bad/truncated payload %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    const bool reuse = !free_meshes.empty();
    const uint32_t id = reuse ? free_meshes.back() : (uint32_t)meshes.size();
    const uint32_t tri_head = index_head / 3;

    if (id >= MAX_MESHES ||
        vertex_head + ph.vertex_count > MAX_VERTS ||
        index_head + ph.index_count > MAX_INDICES ||
        tri_head + ph.tri_count > MAX_TRI_SLOTS ||
        slot_table_head + ph.slot_table_count > MAX_SLOT_TABLE ||
        cluster_head + ph.cluster_count > MAX_CLUSTERS ||
        skinned && skin_head + ph.vertex_count > MAX_SKIN_VERTS ||
        bvhn_head + ph.bvh_node_count > MAX_BVH_NODES) {
        log_write("GeometryPool: arena full loading %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    uint8_t* p = bytes.data() + payload_off;
    uint8_t* p_vtx = p; p += vtx_bytes;
    uint8_t* p_idx = p; p += idx_bytes;
    uint8_t* p_tri = p; p += tri_bytes;
    uint8_t* p_slot = p; p += slot_bytes;
    uint8_t* p_clus = p; p += clus_bytes;
    uint8_t* p_skin = p; p += skin_bytes;
    uint8_t* p_bvhn = p; p += bvhn_bytes;

    Cluster* clus = reinterpret_cast<Cluster*>(p_clus);
    for (uint32_t i = 0; i < ph.cluster_count; i++) clus[i].index_base += index_head;

    uint32_t* idx = reinterpret_cast<uint32_t*>(p_idx);
    for (uint32_t i = 0; i < ph.index_count; i++) idx[i] += vertex_head;
    
    BVHNode* bvhn = reinterpret_cast<BVHNode*>(p_bvhn);
    for (uint32_t i = 0; i < ph.bvh_node_count; i++) {
        if (bvhn[i].left & BVH_LEAF_BIT) {
            bvhn[i].left = BVH_LEAF_BIT | ((bvhn[i].left & ~BVH_LEAF_BIT) + tri_head);
        } else {
            bvhn[i].left += bvhn_head;
            bvhn[i].right += bvhn_head;
        }
    }

    LMesh m{};
    m.vertex_base = vertex_head;
    m.vertex_count = ph.vertex_count;
    m.cluster_base= cluster_head;
    m.cluster_count = ph.cluster_count;
    m.slot_table_base = slot_table_head;
    m.slot_table_count = ph.slot_table_count;
    m.skin_base = skinned ? skin_head : UINT32_MAX;
    m.bvh_node_base = bvhn_head;
    m.bvh_node_count = ph.bvh_node_count;
    m.pos_min = ph.pos_min;
    m.pos_extent = ph.pos_extent;
    m.uv_min = ph.uv_min;
    m.uv_extent = ph.uv_extent;
    m.bounds_sphere = ph.bounds_sphere;

    drivers::DeviceDriverVulkan::BufferUpload uploads[8];
    uint32_t upload_count = 0;
    uploads[upload_count++] = { &vertex_buffer, p_vtx, vtx_bytes, (VkDeviceSize)vertex_head * sizeof(Vertex) };
    uploads[upload_count++] = { &index_buffer, p_idx, idx_bytes, (VkDeviceSize)index_head * sizeof(uint32_t) };
    if (tri_bytes) uploads[upload_count++] = { &tri_slot_buffer, p_tri, tri_bytes, (VkDeviceSize)tri_head * sizeof(uint32_t) };
    if (slot_bytes) uploads[upload_count++] = { &slot_table_buffer, p_slot, slot_bytes, (VkDeviceSize)slot_table_head * sizeof(uint32_t) };
    if (clus_bytes) uploads[upload_count++] = { &cluster_buffer, p_clus, clus_bytes, (VkDeviceSize)cluster_head * sizeof(Cluster) };
    if (skinned && skin_bytes) uploads[upload_count++] = { &skin_buffer, p_skin, skin_bytes, (VkDeviceSize)skin_head * sizeof(SkinVertex) };
    if (bvhn_bytes) uploads[upload_count++] = { &bvh_nodes_buffer, p_bvhn, bvhn_bytes, (VkDeviceSize)bvhn_head * sizeof(BVHNode) };
    uploads[upload_count++] = { &mesh_buffer, &m, sizeof(LMesh), (VkDeviceSize)id * sizeof(LMesh) };

    if (dd->buffer_upload_batch(uploads, upload_count) != Error::Ok) {
        log_write("GeometryPool: upload failed %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    vertex_head += ph.vertex_count;
    index_head += ph.index_count;
    slot_table_head += ph.slot_table_count;
    cluster_head += ph.cluster_count;
    if (skinned) skin_head += ph.vertex_count;
    bvhn_head += ph.bvh_node_count;

    if (reuse) {
        free_meshes.pop_back();
        meshes[id] = m;
        mesh_guids[id] = p_guid;
    } else {
        meshes.push_back(m);
        mesh_guids.push_back(p_guid);
    }
    by_guid.emplace(p_guid, id);

    log_write("GeometryPool: loaded %s verts=%u tris=%u clusters=%u bvh_nodes=%u id=%u", p_path.string().c_str(), ph.vertex_count, ph.tri_count, ph.cluster_count, ph.bvh_node_count, id);
    return id;
}

void GeometryPool::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    const uint32_t id = it->second;

    if (allocated) {
        LMesh empty{};
        drivers::DeviceDriverVulkan::BufferUpload up = { &mesh_buffer, &empty, sizeof(LMesh), (VkDeviceSize)id * sizeof(LMesh) };
        dd->buffer_upload_batch(&up, 1);
    }

    meshes[id] = LMesh{};
    mesh_guids[id] = Guid{};
    free_meshes.push_back(id);
    by_guid.erase(it);
}

void GeometryPool::clear()
{
    vertex_head = index_head = slot_table_head = cluster_head = skin_head = bvhn_head = 0;
    meshes.clear();
    free_meshes.clear();
    by_guid.clear();
}
    
}