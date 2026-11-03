#include <core/assets/lmesh.h>
#include <core/assets/asset_common.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace lumen {

const char* mesh_dag_level_name(MeshDagLevel p_level)
{
    switch (p_level) {
        case MeshDagLevel::FLAT: return "flat";
        case MeshDagLevel::CLUSTERED: return "clustered";
        case MeshDagLevel::CLUSTERED_DAG: return "clustered_dag";
    }
    return "clustered_dag";
}

MeshDagLevel mesh_dag_level_from_name(const std::string& p_name, MeshDagLevel p_fallback)
{
    if (p_name == "flat") return MeshDagLevel::FLAT;
    if (p_name == "clustered") return MeshDagLevel::CLUSTERED;
    if (p_name == "clustered_dag") return MeshDagLevel::CLUSTERED_DAG;
    return p_fallback;
}

bool lmesh_read_stats(const std::filesystem::path& p_bin, LMeshStats& r_stats)
{
    r_stats = {};
    std::error_code ec;
    r_stats.disk_size = std::filesystem::file_size(p_bin, ec);
    if (ec) return false;

    std::ifstream f(p_bin, std::ios::binary);
    LAssetHeader ah{};
    LMeshPayloadHeader ph{};
    if (!f.read(reinterpret_cast<char*>(&ah), sizeof(ah)) || !f.read(reinterpret_cast<char*>(&ph), sizeof(ph))) return false;
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH) return false;

    if (ah.version >= LMESH_VERSION && has_flag((MeshFlags)ph.flags, MeshFlags::SKINNED)) {
        LMeshRigHeader rh{};
        if (!f.read(reinterpret_cast<char*>(&rh), sizeof(rh))) return false;
        r_stats.joints = rh.joint_count;
        f.seekg((std::streamoff)(lmesh_rig_bytes(rh) - sizeof(rh)), std::ios::cur);
    }

    r_stats.vertices = ph.vertex_count;
    r_stats.triangles = ph.tri_count;
    r_stats.slots = ph.slot_count;
    r_stats.clusters = ph.cluster_count;
    r_stats.cluster_triangles = ph.tri_count;
    r_stats.lods = ph.cluster_count ? 1u : 0u;
    r_stats.size = ph.pos_extent;
    if (ah.version < LMESH_VERSION_NO_RIG || ph.cluster_count == 0) return true;

    std::vector<uint32_t> indices(ph.index_count);
    std::vector<Cluster> clusters(ph.cluster_count);
    std::vector<ClusterGroup> groups(ph.group_count);
    const std::streamoff vertex_bytes = (std::streamoff)ph.vertex_count * (sizeof(VertexPosition) + sizeof(VertexAttributes));
    const std::streamoff tri_slot_bytes = (std::streamoff)ph.tri_count * sizeof(uint32_t);
    f.seekg(vertex_bytes, std::ios::cur);
    if (!f.read(reinterpret_cast<char*>(indices.data()), (std::streamsize)(indices.size() * sizeof(uint32_t)))) return true;
    f.seekg(tri_slot_bytes, std::ios::cur);
    if (!f.read(reinterpret_cast<char*>(clusters.data()), (std::streamsize)(clusters.size() * sizeof(Cluster)))) return true;
    if (!groups.empty() && !f.read(reinterpret_cast<char*>(groups.data()), (std::streamsize)(groups.size() * sizeof(ClusterGroup)))) return true;

    std::vector<uint32_t> stamp(ph.vertex_count, UINT32_MAX);
    uint64_t lod0_tris = 0;
    uint64_t cluster_vertices = 0;
    for (uint32_t c = 0; c < ph.cluster_count; ++c) {
        const Cluster& cl = clusters[c];
        if ((uint64_t)cl.index_base + cl.index_count > indices.size()) return true;
        if (cl.self_group == CLUSTER_GROUP_NONE) lod0_tris += cl.index_count / 3;
        for (uint32_t i = 0; i < cl.index_count; ++i) {
            const uint32_t v = indices[cl.index_base + i];
            if (v < ph.vertex_count && stamp[v] != c) {
                stamp[v] = c;
                ++cluster_vertices;
            }
        }
    }

    uint32_t max_level = 0;
    for (const ClusterGroup& g : groups) max_level = std::max(max_level, g.level);

    r_stats.triangles = (uint32_t)lod0_tris;
    r_stats.cluster_vertices = cluster_vertices;
    r_stats.lods = max_level + 1;
    return true;
}

size_t lmesh_rig_bytes(const LMeshRigHeader& p_header)
{
    return sizeof(LMeshRigHeader) + (size_t)p_header.joint_count * sizeof(mat4) + p_header.name_bytes;
}

static bool _read_headers(std::fstream& r_file, LAssetHeader& r_asset, LMeshPayloadHeader& r_payload)
{
    if (!r_file.read(reinterpret_cast<char*>(&r_asset), sizeof(r_asset)) || !r_file.read(reinterpret_cast<char*>(&r_payload), sizeof(r_payload))) return false;
    return r_asset.magic == BCON_MAGIC && r_asset.type == AssetType::MESH && r_asset.version >= LMESH_VERSION && has_flag((MeshFlags)r_payload.flags, MeshFlags::SKINNED);
}

bool lmesh_parse_rig(const uint8_t* p_data, size_t p_size, LMeshRig& r_rig)
{
    r_rig = {};
    LMeshRigHeader rh{};
    if (p_size < sizeof(rh)) return false;
    std::memcpy(&rh, p_data, sizeof(rh));
    if (rh.joint_count > MESH_MAX_JOINTS || p_size < lmesh_rig_bytes(rh)) return false;

    const uint8_t* binds = p_data + sizeof(rh);
    const char* text = reinterpret_cast<const char*>(binds + (size_t)rh.joint_count * sizeof(mat4));
    r_rig.skeleton = rh.skeleton;
    r_rig.inverse_binds.resize(rh.joint_count);
    if (rh.joint_count) std::memcpy(r_rig.inverse_binds.data(), binds, (size_t)rh.joint_count * sizeof(mat4));

    size_t at = 0;
    r_rig.joints.resize(rh.joint_count);
    for (uint32_t i = 0; i < rh.joint_count; ++i) {
        const size_t len = strnlen(text + at, rh.name_bytes - at);
        if (at + len >= rh.name_bytes) return false;
        r_rig.joints[i].assign(text + at, len);
        at += len + 1;
    }
    return true;
}

bool lmesh_read_rig(const std::filesystem::path& p_bin, LMeshRig& r_rig)
{
    r_rig = {};
    std::fstream f(p_bin, std::ios::in | std::ios::binary);
    LAssetHeader ah{};
    LMeshPayloadHeader ph{};
    LMeshRigHeader rh{};
    if (!_read_headers(f, ah, ph) || !f.read(reinterpret_cast<char*>(&rh), sizeof(rh)) || rh.joint_count > MESH_MAX_JOINTS) return false;

    std::vector<uint8_t> block(lmesh_rig_bytes(rh));
    std::memcpy(block.data(), &rh, sizeof(rh));
    if (!f.read(reinterpret_cast<char*>(block.data() + sizeof(rh)), (std::streamsize)(block.size() - sizeof(rh)))) return false;
    return lmesh_parse_rig(block.data(), block.size(), r_rig);
}

bool lmesh_set_skeleton(const std::filesystem::path& p_bin, Guid p_skeleton)
{
    std::fstream f(p_bin, std::ios::in | std::ios::out | std::ios::binary);
    LAssetHeader ah{};
    LMeshPayloadHeader ph{};
    if (!_read_headers(f, ah, ph)) return false;
    f.seekp((std::streamoff)(sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader)));
    f.write(reinterpret_cast<const char*>(&p_skeleton), sizeof(Guid));
    return (bool)f;
}

}
