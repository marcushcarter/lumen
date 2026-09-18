#include <editor/assets/mesh_cooker.h>
#include <editor/editor_context.h>
#include <editor/assets/asset_import_tracker.h>
#include <editor/assets/write_atomic.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/project/project.h>
#include <core/base/tasks.h>
#include <drivers/toml/toml_helpers.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/matrix3x3.h>
#include <meshoptimizer.h>
#include <utils.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <algorithm>
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
    std::vector<ClusterGroup> groups;
    std::vector<BVHNode> bvh_nodes;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

struct SrcVert {
    vec3 p;
    vec3 n;
    vec2 uv;
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

void _gather_node(const aiScene* p_scene, const aiNode* p_node, const aiMatrix4x4& p_parent, std::vector<SrcVert>& r_verts, std::vector<uint32_t>& r_indices, std::vector<SkinVertex>& r_skin, uint32_t& r_bone_base)
{
    const aiMatrix4x4 world = p_parent * p_node->mTransformation;
    const aiMatrix3x3 nrm_mtx(world);

    for (uint32_t i = 0; i < p_node->mNumMeshes; i++) {
        const aiMesh* mesh = p_scene->mMeshes[p_node->mMeshes[i]];
        const uint32_t base = (uint32_t)r_verts.size();

        for (uint32_t vi = 0; vi < mesh->mNumVertices; vi++) {
            const aiVector3D p = world * mesh->mVertices[vi];
            const aiVector3D n = mesh->HasNormals() ? (nrm_mtx * mesh->mNormals[vi]) : aiVector3D(0.0f, 0.0f, 1.0f);
            SrcVert v{};
            v.p = vec3(p.x, p.y, p.z);
            v.n = vec3(n.x, n.y, n.z);
            v.uv = mesh->HasTextureCoords(0) ? vec2(mesh->mTextureCoords[0][vi].x, mesh->mTextureCoords[0][vi].y) : vec2(0.0f);
            r_verts.push_back(v);
        }

        const bool mesh_skinned = mesh->mNumBones > 0;
        if (mesh_skinned || !r_skin.empty()) r_skin.resize(r_verts.size());

        if (mesh_skinned) {
            std::vector<vec4>  acc_w(mesh->mNumVertices, vec4(0.0f));
            std::vector<uvec4> acc_j(mesh->mNumVertices, uvec4(0u));
            std::vector<int>   cnt(mesh->mNumVertices, 0);

            for (uint32_t b = 0; b < mesh->mNumBones; b++) {
                const aiBone* bone = mesh->mBones[b];
                const uint32_t joint = r_bone_base + b;
                for (uint32_t w = 0; w < bone->mNumWeights; w++) {
                    const uint32_t vid = bone->mWeights[w].mVertexId;
                    const float weight = bone->mWeights[w].mWeight;
                    if (cnt[vid] < 4) {
                        const int s = cnt[vid]++;
                        acc_j[vid][s] = joint;
                        acc_w[vid][s] = weight;
                    } else {
                        int mn = 0; for (int k = 1; k < 4; k++) if (acc_w[vid][k] < acc_w[vid][mn]) mn = k;
                        if (weight > acc_w[vid][mn]) { acc_w[vid][mn] = weight; acc_j[vid][mn] = joint; }
                    }
                }
            }

            for (uint32_t vi = 0; vi < mesh->mNumVertices; vi++) {
                vec4 wv = acc_w[vi];
                const float sum = wv.x + wv.y + wv.z + wv.w;
                wv = sum > 0.0f ? wv / sum : vec4(1.0f, 0.0f, 0.0f, 0.0f);
                SkinVertex sv{};
                sv.joints  = u8vec4((uint8_t)glm::min(acc_j[vi].x, 255u), (uint8_t)glm::min(acc_j[vi].y, 255u), (uint8_t)glm::min(acc_j[vi].z, 255u), (uint8_t)glm::min(acc_j[vi].w, 255u));
                sv.weights = u8vec4((uint8_t)glm::round(wv.x * 255.0f), (uint8_t)glm::round(wv.y * 255.0f), (uint8_t)glm::round(wv.z * 255.0f), (uint8_t)glm::round(wv.w * 255.0f));
                r_skin[base + vi] = sv;
            }

            r_bone_base += mesh->mNumBones;
        }

        for (uint32_t fi = 0; fi < mesh->mNumFaces; fi++) {
            const aiFace& face = mesh->mFaces[fi];
            if (face.mNumIndices != 3) continue;
            r_indices.push_back(base + face.mIndices[0]);
            r_indices.push_back(base + face.mIndices[1]);
            r_indices.push_back(base + face.mIndices[2]);
        }
    }

    for (uint32_t c = 0; c < p_node->mNumChildren; c++)
        _gather_node(p_scene, p_node->mChildren[c], world, r_verts, r_indices, r_skin, r_bone_base);
}

bool _gather_scene(const aiScene* p_scene, std::vector<SrcVert>& r_verts, std::vector<uint32_t>& r_indices, std::vector<SkinVertex>& r_skin)
{
    uint32_t bone_base = 0;
    _gather_node(p_scene, p_scene->mRootNode, aiMatrix4x4(), r_verts, r_indices, r_skin, bone_base);
    if (!r_skin.empty()) r_skin.resize(r_verts.size());
    return !r_verts.empty() && !r_indices.empty();
}

void _pack_vertices(const std::vector<SrcVert>& p_verts, MeshSource& r_src)
{
    vec3 pmin = p_verts[0].p, pmax = p_verts[0].p;
    vec2 uvmin = p_verts[0].uv, uvmax = p_verts[0].uv;
    for (const SrcVert& v : p_verts) {
        pmin = glm::min(pmin, v.p); pmax = glm::max(pmax, v.p);
        uvmin = glm::min(uvmin, v.uv); uvmax = glm::max(uvmax, v.uv);
    }
    const vec3 pextent = pmax - pmin;
    const vec2 uvextent = uvmax - uvmin;
    const vec3 inv_pext = vec3(pextent.x > 0.0f ? 1.0f / pextent.x : 0.0f, pextent.y > 0.0f ? 1.0f / pextent.y : 0.0f, pextent.z > 0.0f ? 1.0f / pextent.z : 0.0f);
    const vec2 inv_uvext = vec2(uvextent.x > 0.0f ? 1.0f / uvextent.x : 0.0f, uvextent.y > 0.0f ? 1.0f / uvextent.y : 0.0f);

    r_src.vertices.reserve(p_verts.size());
    for (const SrcVert& v : p_verts) {
        const vec3 np = clamp((v.p - pmin) * inv_pext, 0.0f, 1.0f);
        const vec2 nu = clamp((v.uv - uvmin) * inv_uvext, 0.0f, 1.0f);
        const float nl = length(v.n);
        const vec3 nrm = nl > 1e-6f ? v.n / nl : vec3(0.0f, 0.0f, 1.0f);
        Vertex out{};
        out.position = u16vec3((uint16_t)glm::round(np.x * 65535.0f), (uint16_t)glm::round(np.y * 65535.0f), (uint16_t)glm::round(np.z * 65535.0f));
        out.normal = _oct_encode(nrm);
        out.uv = u16vec2((uint16_t)glm::round(nu.x * 65535.0f), (uint16_t)glm::round(nu.y * 65535.0f));
        r_src.vertices.push_back(out);
    }

    r_src.pos_min = pmin;
    r_src.pos_extent = pextent;
    r_src.uv_min = uvmin;
    r_src.uv_extent = uvextent;
    r_src.bounds_sphere = vec4((pmin + pmax) * 0.5f, length(pextent) * 0.5f);
}

std::pair<uint32_t, uint32_t> _build_meshlets(const std::vector<SrcVert>& p_verts, const std::vector<uint32_t>& p_idx, uint32_t p_self_group, const MeshCooker::CookSettings& p_settings, std::vector<uint32_t>& r_flat, MeshSource& r_src)
{
    const float cone_weight = 0.0f;

    const size_t max_meshlets = meshopt_buildMeshletsBound(p_idx.size(), p_settings.max_verts, p_settings.max_tris);
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
    std::vector<unsigned int> mv(max_meshlets * p_settings.max_verts);
    std::vector<unsigned char> mt(max_meshlets * p_settings.max_tris * 3);

    const size_t count = meshopt_buildMeshlets(meshlets.data(), mv.data(), mt.data(), p_idx.data(), p_idx.size(), &p_verts[0].p.x, p_verts.size(), sizeof(SrcVert), p_settings.max_verts, p_settings.max_tris, cone_weight);

    const uint32_t begin = (uint32_t)r_src.clusters.size();
    for (size_t mi = 0; mi < count; mi++) {
        const meshopt_Meshlet& ml = meshlets[mi];
        meshopt_optimizeMeshlet(&mv[ml.vertex_offset], &mt[ml.triangle_offset], ml.triangle_count, ml.vertex_count);

        const meshopt_Bounds b = meshopt_computeMeshletBounds(&mv[ml.vertex_offset], &mt[ml.triangle_offset], ml.triangle_count, &p_verts[0].p.x, p_verts.size(), sizeof(SrcVert));

        const uint32_t index_base = (uint32_t)r_flat.size();
        for (uint32_t t = 0; t < ml.triangle_count; t++) for (uint32_t k = 0; k < 3; k++) {
            r_flat.push_back(mv[ml.vertex_offset + mt[ml.triangle_offset + t * 3u + k]]);
        }
        const uint32_t index_count = ml.triangle_count * 3u;

        const vec4 cull_sphere = vec4(b.center[0], b.center[1], b.center[2], b.radius);
        r_src.clusters.push_back(Cluster{ index_base, index_count, cull_sphere, p_self_group, CLUSTER_GROUP_NONE });
    }
    return { begin, (uint32_t)r_src.clusters.size() };
}

void _finalize(MeshSource& r_src)
{
    const uint32_t tri_count = (uint32_t)r_src.indices.size() / 3;
    r_src.tri_slots.assign(tri_count, 0u);
    r_src.slot_table = { 0u };

    const vec3 pmin = r_src.pos_min;
    const vec3 pmax = r_src.pos_min + r_src.pos_extent;
    r_src.bvh_nodes.push_back(BVHNode{ pmin, BVH_LEAF_BIT | 0u, pmax, tri_count });
}

bool _load_single_cluster(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, MeshSource& r_src)
{
    std::vector<SrcVert> verts; std::vector<uint32_t> indices; std::vector<SkinVertex> skin;
    if (!_gather_scene(p_scene, verts, indices, skin)) return false;

    if (p_settings.scale != 1.0f) for (SrcVert& v : verts) v.p *= p_settings.scale;

    _pack_vertices(verts, r_src);
    r_src.indices = std::move(indices);
    if (!skin.empty()) r_src.skin_vertices = std::move(skin);

    const vec4 sphere = r_src.bounds_sphere;
    r_src.clusters.push_back(Cluster{ 0u, (uint32_t)r_src.indices.size(), sphere, CLUSTER_GROUP_NONE, CLUSTER_GROUP_NONE });

    _finalize(r_src);
    return true;
}

bool _load_clustered(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, MeshSource& r_src)
{
    std::vector<SrcVert> verts; std::vector<uint32_t> indices; std::vector<SkinVertex> skin;
    if (!_gather_scene(p_scene, verts, indices, skin)) return false;

    if (p_settings.scale != 1.0f) for (SrcVert& v : verts) v.p *= p_settings.scale;

    _pack_vertices(verts, r_src);
    if (!skin.empty()) r_src.skin_vertices = std::move(skin);

    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), verts.size());

    std::vector<uint32_t> flat;
    flat.reserve(indices.size());
    if (_build_meshlets(verts, indices, CLUSTER_GROUP_NONE, p_settings, flat, r_src).first == (uint32_t)r_src.clusters.size()) return false;

    r_src.indices = std::move(flat);
    _finalize(r_src);
    return true;
}

bool _load_clustered_dag(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, MeshSource& r_src)
{
    std::vector<SrcVert> verts; std::vector<uint32_t> indices; std::vector<SkinVertex> skin;
    if (!_gather_scene(p_scene, verts, indices, skin)) return false;

    if (p_settings.scale != 1.0f) for (SrcVert& v : verts) v.p *= p_settings.scale;

    _pack_vertices(verts, r_src);
    if (!skin.empty()) r_src.skin_vertices = std::move(skin);

    const float scale = meshopt_simplifyScale(&verts[0].p.x, verts.size(), sizeof(SrcVert));
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), verts.size());

    std::vector<uint32_t> flat;
    flat.reserve(indices.size() * 2);

    std::pair<uint32_t, uint32_t> lvl0 = _build_meshlets(verts, indices, CLUSTER_GROUP_NONE, p_settings, flat, r_src);
    std::vector<uint32_t> cur;
    for (uint32_t c = lvl0.first; c < lvl0.second; c++) cur.push_back(c);

    for (uint32_t L = 1; L < p_settings.max_levels && cur.size() > 1; L++) {
        const size_t cluster_count = cur.size();

        std::vector<uint32_t> part_idx;
        std::vector<uint32_t> part_counts(cluster_count);
        for (size_t i = 0; i < cluster_count; i++) {
            const Cluster& cl = r_src.clusters[cur[i]];
            part_counts[i] = cl.index_count;
            part_idx.insert(part_idx.end(), flat.begin() + cl.index_base, flat.begin() + cl.index_base + cl.index_count);
        }

        std::vector<uint32_t> part(cluster_count);
        const size_t group_count = meshopt_partitionClusters(part.data(), part_idx.data(), part_idx.size(), part_counts.data(), cluster_count, &verts[0].p.x, verts.size(), sizeof(SrcVert), p_settings.group_size);
        if (group_count == 0) break;

        std::vector<int> owner(verts.size(), -1);
        std::vector<unsigned char> lock(verts.size(), 0);
        for (size_t i = 0; i < cluster_count; i++) {
            const Cluster& cl = r_src.clusters[cur[i]];
            const int g = (int)part[i];
            for (uint32_t k = 0; k < cl.index_count; k++) {
                const uint32_t v = flat[cl.index_base + k];
                if (owner[v] == -1) owner[v] = g;
                else if (owner[v] != g) lock[v] = 1;
            }
        }

        std::vector<uint32_t> next;
        bool any_progress = false;

        for (size_t g = 0; g < group_count; g++) {
            std::vector<uint32_t> members;
            std::vector<uint32_t> gidx;
            std::vector<vec4> child_spheres;
            float child_err = 0.0f;

            for (size_t i = 0; i < cluster_count; i++) {
                if (part[i] != g) continue;
                const uint32_t ci = cur[i];
                const Cluster& cl = r_src.clusters[ci];
                members.push_back(ci);
                gidx.insert(gidx.end(), flat.begin() + cl.index_base, flat.begin() + cl.index_base + cl.index_count);
                const vec4 ss = (cl.self_group == CLUSTER_GROUP_NONE) ? cl.cull_sphere : r_src.groups[cl.self_group].sphere;
                const float se = (cl.self_group == CLUSTER_GROUP_NONE) ? 0.0f : r_src.groups[cl.self_group].error;
                child_spheres.push_back(ss);
                child_err = glm::max(child_err, se);
            }
            if (members.empty()) continue;

            const size_t target = ((gidx.size() / 3) / 2) * 3;
            std::vector<uint32_t> simp(gidx.size());
            float simp_err = 0.0f;
            size_t n = 0;
            if (target >= 3)
                n = meshopt_simplifyWithAttributes(simp.data(), gidx.data(), gidx.size(), &verts[0].p.x, verts.size(), sizeof(SrcVert), nullptr, 0, nullptr, 0, lock.data(), target, FLT_MAX, 0, &simp_err);

            if (n < 3 || n >= gidx.size()) continue;
            simp.resize(n);

            const meshopt_Bounds gb = meshopt_computeSphereBounds(&child_spheres[0].x, child_spheres.size(), sizeof(vec4), &child_spheres[0].w, sizeof(vec4));
            const uint32_t gid = (uint32_t)r_src.groups.size();
            r_src.groups.push_back(ClusterGroup{ vec4(gb.center[0], gb.center[1], gb.center[2], gb.radius), child_err + simp_err * scale, 0u });

            for (uint32_t ci : members) r_src.clusters[ci].parent_group = gid;

            std::pair<uint32_t, uint32_t> ng = _build_meshlets(verts, simp, gid, p_settings, flat, r_src);
            for (uint32_t c = ng.first; c < ng.second; c++) next.push_back(c);
            any_progress = true;
        }

        if (!any_progress) break;
        cur = std::move(next);
    }

    r_src.indices = std::move(flat);
    _finalize(r_src);
    return true;
}

Error MeshCooker::_cook(const Job& p_job)
{
    using enum Error;

    auto report = [&](float v){ if (p_job.progress) p_job.progress->progress.store(v, std::memory_order_relaxed); };
    report(0.0f);

    Assimp::Importer importer;
    const unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords | aiProcess_LimitBoneWeights | aiProcess_ImproveCacheLocality;
    const aiScene* scene = importer.ReadFile(p_job.source.string(), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode || scene->mNumMeshes == 0) {
        log_write("MeshCooker: assimp failed for %s (%s)", p_job.source.string().c_str(), importer.GetErrorString());
        return Failed;
    }
    
    report(0.3f);

    MeshSource src;
    bool ok = false;
    switch (p_job.settings.dag_level) {
        case 0: ok = _load_single_cluster(scene, p_job.settings, src); break;
        case 1: ok = _load_clustered(scene, p_job.settings, src); break;
        default: ok = _load_clustered_dag(scene, p_job.settings, src); break;
    }
    if (!ok) { log_write("MeshCooker: %s produced no geometry", p_job.source.string().c_str()); return Failed; }

    report(0.8f);

    const bool skinned = !src.skin_vertices.empty();

    LMeshPayloadHeader ph{};
    ph.vertex_count = (uint32_t)src.vertices.size();
    ph.index_count = (uint32_t)src.indices.size();
    ph.tri_count = ph.index_count / 3;
    ph.slot_table_count = (uint32_t)src.slot_table.size();
    ph.cluster_count = (uint32_t)src.clusters.size();
    ph.group_count = (uint32_t)src.groups.size();
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
    const size_t group_bytes = src.groups.size() * sizeof(ClusterGroup);
    const size_t skin_bytes = src.skin_vertices.size() * sizeof(SkinVertex);
    const size_t bvhn_bytes = src.bvh_nodes.size() * sizeof(BVHNode);

    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LASSET_VERSION;
    ah.guid = p_job.guid;
    ah.type = AssetType::Mesh;
    ah.payload_size = (uint32_t)(sizeof(ph) + vtx_bytes + idx_bytes + tri_bytes + slot_bytes + clus_bytes + group_bytes + skin_bytes + bvhn_bytes);

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
    if (group_bytes) f.write(reinterpret_cast<const char*>(src.groups.data()), group_bytes);
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
            { "dag_level", p_job.settings.dag_level },
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