#include <editor/assets/mesh_cooker.h>
#include <editor/editor_context.h>
#include <editor/assets/asset_import_tracker.h>
#include <editor/assets/write_atomic.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/project/project.h>
#include <core/base/tasks.h>
#include <drivers/toml/toml_helpers.h>
// #include <vulkan/vulkan.h>
// #include <rdo_bc_encoder.h>
#include <utils.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <cfloat>

namespace lumen {

using namespace glm;

struct MeshSource {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> tri_slots;
    std::vector<uint32_t> slot_table;
    std::vector<SkinVertex> skin_vertices;
    std::vector<Cluster> clusters;
    std::vector<BVHNode> bvh_nodes;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

static i16vec2 _oct_encode(vec3 n)
{
    const float s = abs(n.x) + abs(n.y) + abs(n.z);
    if (s > 0.0f) n /= s;
    vec2 e = vec2(n.x, n.y);
    if (n.z < 0.0f) e = (1.0f - abs(vec2(e.y, e.x))) * vec2(e.x >= 0.0f ? 1.0f : -1.0f, e.y >= 0.0f ? 1.0f : -1.0f);
    e = clamp(e, -1.0f, 1.0f);
    return i16vec2((int16_t)glm::round(e.x * 32767.0f), (int16_t)glm::round(e.y * 32767.0f));
}

Error MeshCooker::_cook(const Job& p_job)
{
    using enum Error;

    auto report = [&](float v){ if (p_job.progress) p_job.progress->progress.store(v, std::memory_order_relaxed); };
    report(0.0f);

    // load mesh with assimp
        
    report(0.1f);

    MeshSource src;

    if (p_job.settings.dag) {

    } else {

    }

    {
        struct SrcVert { vec3 p; vec3 n; vec2 uv; };
        const SrcVert verts[3] = {
            { vec3( 0.0f,  0.5f, 0.0f), vec3(0.0f, 0.0f, 1.0f), vec2(0.5f, 1.0f) },
            { vec3(-0.5f, -0.5f, 0.0f), vec3(0.0f, 0.0f, 1.0f), vec2(0.0f, 0.0f) },
            { vec3( 0.5f, -0.5f, 0.0f), vec3(0.0f, 0.0f, 1.0f), vec2(1.0f, 0.0f) },
        };

        vec3 pmin = verts[0].p, pmax = verts[0].p;
        vec2 uvmin = verts[0].uv, uvmax = verts[0].uv;
        for (const SrcVert& v : verts) {
            pmin = glm::min(pmin, v.p); pmax = glm::max(pmax, v.p);
            uvmin = glm::min(uvmin, v.uv); uvmax = glm::max(uvmax, v.uv);
        }
        const vec3 pextent = pmax - pmin;
        const vec2 uvextent = uvmax - uvmin;
        const vec3 inv_pext = vec3(pextent.x > 0.0f ? 1.0f / pextent.x : 0.0f, pextent.y > 0.0f ? 1.0f / pextent.y : 0.0f, pextent.z > 0.0f ? 1.0f / pextent.z : 0.0f);
        const vec2 inv_uvext = vec2(uvextent.x > 0.0f ? 1.0f / uvextent.x : 0.0f, uvextent.y > 0.0f ? 1.0f / uvextent.y : 0.0f);

        for (const SrcVert& v : verts) {
            const vec3 np = clamp((v.p - pmin) * inv_pext, 0.0f, 1.0f);
            const vec2 nu = clamp((v.uv - uvmin) * inv_uvext, 0.0f, 1.0f);
            Vertex out{};
            out.position = u16vec3((uint16_t)glm::round(np.x * 65535.0f), (uint16_t)glm::round(np.y * 65535.0f), (uint16_t)glm::round(np.z * 65535.0f));
            out.normal = _oct_encode(v.n);
            out.uv = u16vec2((uint16_t)glm::round(nu.x * 65535.0f), (uint16_t)glm::round(nu.y * 65535.0f));
            src.vertices.push_back(out);
        }

        src.pos_min = pmin;
        src.pos_extent = pextent;
        src.uv_min = uvmin;
        src.uv_extent = uvextent;
        src.bounds_sphere = vec4((pmin + pmax) * 0.5f, length(pextent) * 0.5f);

        src.indices = { 0, 1, 2 };
        src.tri_slots = { 0 };
        src.slot_table = { 0 };

        const vec4 sphere = src.bounds_sphere;
        src.clusters.push_back(Cluster{ 0, 3, sphere, vec4(0.0f, 0.0f, 1.0f, 1.0f), sphere, sphere, 0.0f, FLT_MAX, });

        src.bvh_nodes.push_back(BVHNode{ pmin, BVH_LEAF_BIT | 0u, pmax, 1u });
    }

    const bool skinned = !src.skin_vertices.empty();

    LMeshPayloadHeader ph{};
    ph.vertex_count = (uint32_t)src.vertices.size();
    ph.index_count = (uint32_t)src.indices.size();
    ph.tri_count = ph.index_count / 3;
    ph.slot_table_count = (uint32_t)src.slot_table.size();
    ph.cluster_count = (uint32_t)src.clusters.size();
    ph.bvh_node_count = (uint32_t)src.bvh_nodes.size();
    ph.flags = skinned ? MESH_FLAG_SKINNED : 0u;
    ph.pos_min = src.pos_min;
    ph.pos_extent = src.pos_extent;
    ph.uv_min = src.uv_min;
    ph.uv_extent = src.uv_extent;
    ph.bounds_sphere = src.bounds_sphere;

    const size_t vtx_bytes = src.vertices.size() * sizeof(Vertex);
    const size_t idx_bytes = src.indices.size() * sizeof(uint32_t);
    const size_t tri_bytes = src.tri_slots.size() * sizeof(uint32_t);
    const size_t slot_bytes = src.slot_table.size() * sizeof(uint32_t);
    const size_t clus_bytes = src.clusters.size() * sizeof(Cluster);
    const size_t skin_bytes = src.skin_vertices.size() * sizeof(SkinVertex);
    const size_t bvhn_bytes = src.bvh_nodes.size() * sizeof(BVHNode);

    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LASSET_VERSION;
    ah.guid = p_job.guid;
    ah.type = AssetType::Mesh;
    ah.payload_size = (uint32_t)(sizeof(ph) + vtx_bytes + idx_bytes + tri_bytes + slot_bytes + clus_bytes + skin_bytes + bvhn_bytes);

    std::error_code ec;
    std::filesystem::create_directories(p_job.content_bin.parent_path(), ec);
    std::ofstream f(p_job.content_bin, std::ios::binary | std::ios::trunc);
    if (!f) { return Failed; }
    f.write(reinterpret_cast<const char*>(&ah), sizeof(ah));
    f.write(reinterpret_cast<const char*>(&ph), sizeof(ph));
    f.write(reinterpret_cast<const char*>(src.vertices.data()), vtx_bytes);
    f.write(reinterpret_cast<const char*>(src.indices.data()), idx_bytes);
    if (tri_bytes) f.write(reinterpret_cast<const char*>(src.tri_slots.data()), tri_bytes);
    if (slot_bytes) f.write(reinterpret_cast<const char*>(src.slot_table.data()), slot_bytes);
    if (clus_bytes) f.write(reinterpret_cast<const char*>(src.clusters.data()), clus_bytes);
    if (skin_bytes) f.write(reinterpret_cast<const char*>(src.skin_vertices.data()), skin_bytes);
    if (bvhn_bytes) f.write(reinterpret_cast<const char*>(src.bvh_nodes.data()), bvhn_bytes);

    report(0.95f);

    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_job.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::Mesh) } }},
        { std::string(asset_type_section(AssetType::Mesh)), toml::table{
            { "path", p_job.source.generic_string() },
            { "vertices", static_cast<int64_t>(0) },
            { "triangles", static_cast<int64_t>(0) },
            { "clusters", static_cast<int64_t>(0) },
            { "skinned", skinned },
            { "dag", p_job.settings.dag },
            }},
    };
    std::string text; { std::ostringstream ss; ss << tbl; text = ss.str(); }
    if (!write_file_atomic(p_job.dst_lmesh, text.data(), text.size())) return Failed;

    report(1.0f);
    return Ok;
}

Error MeshCooker::import(const Project& p_project, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, Guid& r_guid, const CookSettings& p_settings)
{
    using enum Error;
    Job job;
    job.source = p_src;
    job.dst_lmesh = p_dst;
    job.settings = p_settings;
    if (AssetImportTracker::resolve_import(p_project, AssetType::Mesh, p_dst, job.guid, job.content_bin) != Ok) return Failed;
    if (_cook(job) != Ok) return Failed;
    r_guid = job.guid;
    return Ok;   
}

void MeshCooker::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const MeshCooker::CookSettings& p_settings)
{
    Job job;
    job.source = p_src;
    job.dst_lmesh = p_dst;
    job.settings = p_settings;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::Mesh, p_dst, job.guid, job.content_bin) != Error::Ok) {
        log_write("Mesh import failed: %s", p_src.string().c_str());
        return;
    }
    job.progress = ctx.imports->add(p_dst, job.guid, job.content_bin);
    ctx.tasks->dispatch([job]{
        if (MeshCooker::_cook(job) != Error::Ok && job.progress && !job.progress->cancel.load(std::memory_order_relaxed)) job.progress->progress.store(1.0f, std::memory_order_relaxed);
    });
}

}