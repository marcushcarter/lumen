#include <core/assets/lmesh.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <cstddef>
#include <cfloat>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace lumen {

struct LMeshLegacyHeader
{
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t tri_count;
    uint32_t slot_count;
    uint32_t cluster_count;
    uint32_t group_count;
    uint32_t bvh_node_count;
    uint32_t flags;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

static_assert(sizeof(LMeshLegacyHeader) == 88, "legacy lmesh header layout is frozen");

static constexpr size_t LEGACY_BVH_NODE_BYTES = 32;

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

static size_t _payload_header_bytes(uint32_t p_version)
{
    return p_version >= LMESH_VERSION_PAGED ? sizeof(LMeshPayloadHeader) : sizeof(LMeshLegacyHeader);
}

static uint32_t _payload_flags(uint32_t p_version, const uint8_t* p_header)
{
    uint32_t flags = 0;
    const size_t at = p_version >= LMESH_VERSION_PAGED ? offsetof(LMeshPayloadHeader, flags) : offsetof(LMeshLegacyHeader, flags);
    std::memcpy(&flags, p_header + at, sizeof(flags));
    return flags;
}

static bool _has_rig_block(uint32_t p_version, uint32_t p_flags)
{
    return p_version >= LMESH_VERSION_RIG && has_flag((MeshFlags)p_flags, MeshFlags::SKINNED);
}

static vec4 _sphere_union(const std::vector<vec4>& p_spheres)
{
    if (p_spheres.empty()) return vec4(0.0f);
    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (const vec4& s : p_spheres) {
        lo = glm::min(lo, vec3(s) - s.w);
        hi = glm::max(hi, vec3(s) + s.w);
    }
    const vec3 c = (lo + hi) * 0.5f;
    float r = 0.0f;
    for (const vec4& s : p_spheres) r = glm::max(r, length(vec3(s) - c) + s.w);
    return vec4(c, r);
}

static uint32_t _expand_bits10(uint32_t p_v)
{
    p_v &= 1023u;
    p_v = (p_v | (p_v << 16)) & 0x030000FFu;
    p_v = (p_v | (p_v << 8)) & 0x0300F00Fu;
    p_v = (p_v | (p_v << 4)) & 0x030C30C3u;
    p_v = (p_v | (p_v << 2)) & 0x09249249u;
    return p_v;
}

static uint32_t _morton(vec3 p_point, vec3 p_min, vec3 p_extent)
{
    const vec3 n = clamp((p_point - p_min) / glm::max(p_extent, vec3(1e-6f)), 0.0f, 1.0f) * 1023.0f;
    return _expand_bits10((uint32_t)n.x) | (_expand_bits10((uint32_t)n.y) << 1) | (_expand_bits10((uint32_t)n.z) << 2);
}

static const char* _split_clusters(const std::vector<Cluster>& p_in, const std::vector<uint32_t>& p_indices, uint32_t p_vertex_count, std::vector<Cluster>& r_out)
{
    const uint32_t index_count = (uint32_t)p_indices.size();
    std::vector<uint32_t> stamp(p_vertex_count, UINT32_MAX);
    uint32_t chunk_id = 0;
    r_out.clear();
    r_out.reserve(p_in.size());
    for (const Cluster& src : p_in) {
        if ((uint64_t)src.index_base + src.index_count > index_count) return "cluster index range out of bounds";
        if (src.index_base % 3 != 0 || src.index_count % 3 != 0) return "cluster index range not triangle aligned";
        if (src.index_count == 0) continue;
        if (src.index_count / 3 <= LMESH_CLUSTER_MAX_TRIS && src.index_count <= LMESH_CLUSTER_MAX_VERTS) {
            r_out.push_back(src);
            continue;
        }

        Cluster chunk = src;
        chunk.index_count = 0;
        uint32_t chunk_verts = 0;
        chunk_id++;
        for (uint32_t i = 0; i < src.index_count; i += 3) {
            uint32_t fresh = 0;
            for (uint32_t k = 0; k < 3; k++) {
                if (stamp[p_indices[src.index_base + i + k]] != chunk_id) fresh++;
            }
            if (chunk.index_count / 3 + 1 > LMESH_CLUSTER_MAX_TRIS || chunk_verts + fresh > LMESH_CLUSTER_MAX_VERTS) {
                r_out.push_back(chunk);
                chunk.index_base += chunk.index_count;
                chunk.index_count = 0;
                chunk_verts = 0;
                chunk_id++;
            }
            for (uint32_t k = 0; k < 3; k++) {
                const uint32_t v = p_indices[src.index_base + i + k];
                if (stamp[v] != chunk_id) {
                    stamp[v] = chunk_id;
                    chunk_verts++;
                }
            }
            chunk.index_count += 3;
        }
        if (chunk.index_count) r_out.push_back(chunk);
    }
    return nullptr;
}

static void _compute_child_pages(LMeshData& r_data);

const char* lmesh_build(const LMeshSource& p_src, LMeshData& r_data)
{
    r_data = {};
    const uint32_t vertex_count = (uint32_t)p_src.positions.size();
    if (vertex_count == 0 || p_src.attributes.size() != vertex_count) return "bad vertex streams";
    if (!p_src.skin.empty() && p_src.skin.size() != vertex_count) return "bad skin stream";
    if (p_src.indices.empty() || p_src.indices.size() % 3 != 0) return "bad index count";
    const uint32_t src_tri_count = (uint32_t)p_src.indices.size() / 3;
    if (p_src.tri_slots.size() != src_tri_count) return "bad triangle slot count";
    if (p_src.slot_count == 0) return "mesh has no material slots";
    for (uint32_t v : p_src.indices) if (v >= vertex_count) return "index out of range";
    for (uint32_t s : p_src.tri_slots) if (s >= p_src.slot_count) return "triangle slot out of range";

    const uint32_t group_count = (uint32_t)p_src.groups.size();
    std::vector<Cluster> input = p_src.clusters;
    if (input.empty()) input.push_back(Cluster{ 0u, (uint32_t)p_src.indices.size(), p_src.bounds_sphere, CLUSTER_GROUP_NONE, CLUSTER_GROUP_NONE });
    std::vector<Cluster> clusters;
    if (const char* why = _split_clusters(input, p_src.indices, vertex_count, clusters)) return why;
    if (clusters.empty()) return "mesh has no clusters";
    for (const Cluster& c : clusters) {
        if (c.self_group != CLUSTER_GROUP_NONE && c.self_group >= group_count) return "cluster self_group out of range";
        if (c.parent_group != CLUSTER_GROUP_NONE && c.parent_group >= group_count) return "cluster parent_group out of range";
    }

    const bool skinned = !p_src.skin.empty();
    const bool wide = p_src.slot_count > LMESH_LOCAL_SLOT_OVERFLOW;
    const uint32_t cluster_count = (uint32_t)clusters.size();

    struct Local {
        uint32_t vertex_first, vertex_count;
        uint32_t tri_first, tri_count;
        uint32_t slot;
        uint32_t bounds[3];
    };
    std::vector<Local> local(cluster_count);
    std::vector<uint32_t> local_vertices;
    std::vector<uint32_t> local_tris;
    std::vector<uint32_t> local_slots;
    local_vertices.reserve(p_src.indices.size() / 2);
    local_tris.reserve(p_src.indices.size() / 3);
    {
        std::vector<uint32_t> stamp(vertex_count, UINT32_MAX);
        std::vector<uint8_t> local_of(vertex_count, 0);
        for (uint32_t ci = 0; ci < cluster_count; ci++) {
            const Cluster& cl = clusters[ci];
            Local& l = local[ci];
            l.vertex_first = (uint32_t)local_vertices.size();
            l.tri_first = (uint32_t)local_tris.size();
            l.tri_count = cl.index_count / 3;
            if (l.tri_count > LMESH_CLUSTER_MAX_TRIS) return "cluster has too many triangles";
            uint32_t uniform = p_src.tri_slots[cl.index_base / 3];
            for (uint32_t i = 0; i < cl.index_count; i += 3) {
                uint32_t packed = 0;
                for (uint32_t k = 0; k < 3; k++) {
                    const uint32_t v = p_src.indices[cl.index_base + i + k];
                    if (stamp[v] != ci) {
                        const uint32_t n = (uint32_t)local_vertices.size() - l.vertex_first;
                        if (n >= LMESH_CLUSTER_MAX_VERTS) return "cluster has too many unique vertices";
                        stamp[v] = ci;
                        local_of[v] = (uint8_t)n;
                        local_vertices.push_back(v);
                    }
                    packed |= (uint32_t)local_of[v] << (8 * k);
                }
                const uint32_t slot = p_src.tri_slots[(cl.index_base + i) / 3];
                local_tris.push_back(packed | (std::min(slot, LMESH_LOCAL_SLOT_OVERFLOW) << 24));
                if (wide) local_slots.push_back(slot);
                if (slot != uniform) uniform = LMESH_CLUSTER_SLOT_MIXED;
            }
            l.vertex_count = (uint32_t)local_vertices.size() - l.vertex_first;
            l.slot = uniform < LMESH_LOCAL_SLOT_OVERFLOW ? uniform : LMESH_CLUSTER_SLOT_MIXED;

            u16vec3 lo(UINT16_MAX), hi(0);
            for (uint32_t i = 0; i < l.vertex_count; i++) {
                const u16vec3 q = p_src.positions[local_vertices[l.vertex_first + i]].position;
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
            if (l.vertex_count == 0) lo = hi = u16vec3(0);
            l.bounds[0] = (uint32_t)lo.x | ((uint32_t)lo.y << 16);
            l.bounds[1] = (uint32_t)lo.z | ((uint32_t)hi.x << 16);
            l.bounds[2] = (uint32_t)hi.y | ((uint32_t)hi.z << 16);
        }
    }

    auto level_of = [&](const Cluster& c) { return c.self_group == CLUSTER_GROUP_NONE ? 0u : p_src.groups[c.self_group].level; };

    struct Part {
        uint32_t self = CLUSTER_GROUP_NONE;
        uint32_t parent = CLUSTER_GROUP_NONE;
        uint32_t level = 0;
        uint32_t unit = 0;
        uint32_t morton = 0;
        bool root = false;
        std::vector<uint32_t> clusters;
        uint32_t first_cluster = 0;
        uint32_t page = 0;
    };
    std::vector<Part> parts;
    {
        std::unordered_map<uint64_t, uint32_t> by_key;
        std::vector<uint32_t> loose;
        for (uint32_t ci = 0; ci < cluster_count; ci++) {
            const Cluster& c = clusters[ci];
            if (c.self_group == CLUSTER_GROUP_NONE && c.parent_group == CLUSTER_GROUP_NONE) {
                loose.push_back(ci);
                continue;
            }
            const uint64_t key = ((uint64_t)c.self_group << 32) | c.parent_group;
            auto [it, fresh] = by_key.try_emplace(key, (uint32_t)parts.size());
            if (fresh) {
                Part p;
                p.self = c.self_group;
                p.parent = c.parent_group;
                p.level = level_of(c);
                p.root = c.parent_group == CLUSTER_GROUP_NONE;
                parts.push_back(std::move(p));
            }
            parts[it->second].clusters.push_back(ci);
        }
        std::sort(loose.begin(), loose.end(), [&](uint32_t a, uint32_t b) {
            return _morton(vec3(clusters[a].cull_sphere), p_src.pos_min, p_src.pos_extent) < _morton(vec3(clusters[b].cull_sphere), p_src.pos_min, p_src.pos_extent);
        });
        for (size_t i = 0; i < loose.size(); i += LMESH_PART_MAX_CLUSTERS) {
            Part p;
            p.root = true;
            p.clusters.assign(loose.begin() + i, loose.begin() + std::min(loose.size(), i + LMESH_PART_MAX_CLUSTERS));
            parts.push_back(std::move(p));
        }
    }
    for (uint32_t i = 0; i < (uint32_t)parts.size(); i++) {
        Part& p = parts[i];
        vec3 center(0.0f);
        if (p.self != CLUSTER_GROUP_NONE) {
            p.unit = p.self;
            center = vec3(p_src.groups[p.self].sphere);
        } else {
            p.unit = group_count + i;
            for (uint32_t ci : p.clusters) center += vec3(clusters[ci].cull_sphere);
            center /= (float)p.clusters.size();
        }
        p.morton = _morton(center, p_src.pos_min, p_src.pos_extent);
    }
    std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
        if (a.root != b.root) return a.root;
        if (a.level != b.level) return a.level > b.level;
        if (a.morton != b.morton) return a.morton < b.morton;
        if (a.unit != b.unit) return a.unit < b.unit;
        return a.parent < b.parent;
    });

    auto cluster_bytes = [&](uint32_t ci) {
        size_t b = sizeof(LMeshCluster) + (size_t)local[ci].tri_count * sizeof(uint32_t) + (size_t)local[ci].vertex_count * sizeof(ClusterVertex);
        if (skinned) b += (size_t)local[ci].vertex_count * sizeof(SkinVertex);
        if (wide) b += (size_t)local[ci].tri_count * sizeof(uint32_t);
        return b;
    };

    std::vector<std::pair<uint32_t, uint32_t>> page_parts;
    {
        size_t page_bytes = 0;
        bool page_root = false;
        for (uint32_t i = 0; i < (uint32_t)parts.size();) {
            uint32_t j = i;
            size_t run_bytes = 0;
            while (j < parts.size() && parts[j].unit == parts[i].unit && parts[j].root == parts[i].root) {
                for (uint32_t ci : parts[j].clusters) run_bytes += cluster_bytes(ci);
                j++;
            }
            const bool open = !page_parts.empty() && page_bytes > 0;
            if (!open || page_root != parts[i].root || page_bytes + run_bytes > LMESH_PAGE_BYTES) {
                page_parts.push_back({ i, j });
                page_bytes = 0;
                page_root = parts[i].root;
            } else {
                page_parts.back().second = j;
            }
            page_bytes += run_bytes;
            i = j;
        }
    }

    for (uint32_t pi = 0; pi < (uint32_t)page_parts.size(); pi++) {
        LMeshPage page{};
        page.cluster_first = (uint32_t)r_data.clusters.size();
        page.tri_first = (uint32_t)r_data.local_tris.size();
        page.vertex_first = (uint32_t)r_data.vertices.size();
        for (uint32_t i = page_parts[pi].first; i < page_parts[pi].second; i++) {
            Part& part = parts[i];
            part.first_cluster = (uint32_t)r_data.clusters.size();
            part.page = pi;
            if (part.root) r_data.header.root_page_count = pi + 1;
            for (uint32_t ci : part.clusters) {
                const Cluster& c = clusters[ci];
                const Local& l = local[ci];
                LMeshCluster out{};
                out.cull_sphere = c.cull_sphere;
                out.self_group = c.self_group;
                out.parent_group = c.parent_group;
                out.tri_offset = (uint32_t)r_data.local_tris.size() - page.tri_first;
                out.vertex_offset = (uint32_t)r_data.vertices.size() - page.vertex_first;
                out.tri_count = l.tri_count;
                out.vertex_count = l.vertex_count;
                out.slot = l.slot;
                std::memcpy(out.bounds, l.bounds, sizeof(out.bounds));
                r_data.clusters.push_back(out);
                r_data.local_tris.insert(r_data.local_tris.end(), local_tris.begin() + l.tri_first, local_tris.begin() + l.tri_first + l.tri_count);
                if (wide) r_data.tri_slots.insert(r_data.tri_slots.end(), local_slots.begin() + l.tri_first, local_slots.begin() + l.tri_first + l.tri_count);
                for (uint32_t v = 0; v < l.vertex_count; v++) {
                    const uint32_t g = local_vertices[l.vertex_first + v];
                    r_data.vertices.push_back(ClusterVertex{ p_src.positions[g], p_src.attributes[g] });
                    if (skinned) r_data.skin.push_back(p_src.skin[g]);
                }
            }
        }
        page.cluster_count = (uint32_t)r_data.clusters.size() - page.cluster_first;
        page.tri_count = (uint32_t)r_data.local_tris.size() - page.tri_first;
        page.vertex_count = (uint32_t)r_data.vertices.size() - page.vertex_first;
        r_data.pages.push_back(page);
    }

    struct TreeNode {
        LMeshNode node{};
        std::vector<uint32_t> kids;
    };
    std::vector<TreeNode> tree;
    tree.reserve(parts.size() * 2);
    for (const Part& part : parts) {
        TreeNode t;
        std::vector<vec4> spheres;
        for (uint32_t i = 0; i < (uint32_t)part.clusters.size(); i++) spheres.push_back(r_data.clusters[part.first_cluster + i].cull_sphere);
        t.node.cull_sphere = _sphere_union(spheres);
        if (part.parent == CLUSTER_GROUP_NONE) {
            t.node.lod_sphere = t.node.cull_sphere;
            t.node.max_parent_error = FLT_MAX;
        } else {
            t.node.lod_sphere = p_src.groups[part.parent].sphere;
            t.node.max_parent_error = p_src.groups[part.parent].error;
        }
        t.node.first = part.first_cluster;
        t.node.count = (uint32_t)part.clusters.size();
        t.node.page = part.page;
        tree.push_back(std::move(t));
    }

    auto merge_layer = [&](const std::vector<uint32_t>& p_layer) {
        std::vector<uint32_t> up;
        for (size_t i = 0; i < p_layer.size(); i += LMESH_NODE_FANOUT) {
            TreeNode t;
            std::vector<vec4> cull, lod;
            float err = 0.0f;
            for (size_t k = i; k < std::min(p_layer.size(), i + LMESH_NODE_FANOUT); k++) {
                const LMeshNode& kid = tree[p_layer[k]].node;
                t.kids.push_back(p_layer[k]);
                cull.push_back(kid.cull_sphere);
                lod.push_back(kid.lod_sphere);
                err = glm::max(err, kid.max_parent_error);
            }
            t.node.cull_sphere = _sphere_union(cull);
            t.node.lod_sphere = _sphere_union(lod);
            t.node.max_parent_error = err;
            t.node.page = LMESH_NODE_INNER;
            up.push_back((uint32_t)tree.size());
            tree.push_back(std::move(t));
        }
        return up;
    };

    std::vector<uint32_t> run_roots;
    for (uint32_t i = 0; i < (uint32_t)parts.size();) {
        uint32_t j = i;
        std::vector<uint32_t> layer;
        while (j < parts.size() && parts[j].root == parts[i].root && parts[j].level == parts[i].level) layer.push_back(j++);
        while (layer.size() > 1) layer = merge_layer(layer);
        run_roots.push_back(layer[0]);
        i = j;
    }
    while (run_roots.size() > 1) run_roots = merge_layer(run_roots);

    std::vector<uint32_t> order{ run_roots[0] };
    for (size_t h = 0; h < order.size(); h++) {
        const TreeNode& t = tree[order[h]];
        LMeshNode out = t.node;
        if (!t.kids.empty()) {
            out.first = (uint32_t)order.size();
            out.count = (uint32_t)t.kids.size();
            order.insert(order.end(), t.kids.begin(), t.kids.end());
        }
        r_data.nodes.push_back(out);
    }

    uint32_t lod0_tris = 0;
    uint32_t max_level = 0;
    for (const LMeshCluster& c : r_data.clusters) if (c.self_group == CLUSTER_GROUP_NONE) lod0_tris += c.tri_count;
    for (const ClusterGroup& g : p_src.groups) max_level = std::max(max_level, g.level);

    LMeshPayloadHeader& h = r_data.header;
    h.flags = (uint32_t)((skinned ? MeshFlags::SKINNED : MeshFlags::NONE) | (wide ? MeshFlags::WIDE_SLOTS : MeshFlags::NONE));
    h.slot_count = p_src.slot_count;
    h.source_vertex_count = vertex_count;
    h.lod0_tri_count = lod0_tris;
    h.cluster_count = (uint32_t)r_data.clusters.size();
    h.tri_count = (uint32_t)r_data.local_tris.size();
    h.vertex_count = (uint32_t)r_data.vertices.size();
    h.group_count = group_count;
    h.node_count = (uint32_t)r_data.nodes.size();
    h.page_count = (uint32_t)r_data.pages.size();
    h.level_count = max_level + 1;
    h.pos_min = p_src.pos_min;
    h.pos_extent = p_src.pos_extent;
    h.uv_min = p_src.uv_min;
    h.uv_extent = p_src.uv_extent;
    h.bounds_sphere = p_src.bounds_sphere;
    r_data.groups = p_src.groups;
    r_data.rig = p_src.rig;
    _compute_child_pages(r_data);
    return lmesh_validate(r_data);
}

static void _compute_child_pages(LMeshData& r_data)
{
    const uint32_t group_count = (uint32_t)r_data.groups.size();
    std::vector<std::vector<uint32_t>> kids(group_count);
    for (uint32_t pi = 0; pi < (uint32_t)r_data.pages.size(); pi++) {
        const LMeshPage& page = r_data.pages[pi];
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
            const uint32_t g = r_data.clusters[ci].parent_group;
            if (g == CLUSTER_GROUP_NONE || g >= group_count) continue;
            if (kids[g].empty() || kids[g].back() != pi) kids[g].push_back(pi);
        }
    }
    r_data.group_child_first.assign(group_count + 1, 0u);
    r_data.group_child_pages.clear();
    for (uint32_t g = 0; g < group_count; g++) {
        r_data.group_child_first[g] = (uint32_t)r_data.group_child_pages.size();
        r_data.group_child_pages.insert(r_data.group_child_pages.end(), kids[g].begin(), kids[g].end());
    }
    r_data.group_child_first[group_count] = (uint32_t)r_data.group_child_pages.size();
    r_data.header.group_child_page_count = (uint32_t)r_data.group_child_pages.size();
}

const char* lmesh_validate_tables(const LMeshData& p_data)
{
    const LMeshPayloadHeader& h = p_data.header;
    if (h.slot_count == 0) return "mesh has no material slots";
    if (h.cluster_count == 0 || h.page_count == 0 || h.node_count == 0) return "mesh has no clusters";
    if (p_data.clusters.size() != h.cluster_count || p_data.groups.size() != h.group_count || p_data.nodes.size() != h.node_count || p_data.pages.size() != h.page_count) return "table sizes disagree with the header";
    if (h.root_page_count == 0 || h.root_page_count > h.page_count) return "bad root page count";
    if (p_data.group_child_first.size() != (size_t)h.group_count + 1 || p_data.group_child_pages.size() != h.group_child_page_count) return "bad group child page table";
    if (p_data.group_child_first[0] != 0 || p_data.group_child_first[h.group_count] != h.group_child_page_count) return "bad group child page table";
    for (uint32_t g = 0; g < h.group_count; g++) if (p_data.group_child_first[g] > p_data.group_child_first[g + 1]) return "bad group child page table";
    for (uint32_t p : p_data.group_child_pages) if (p >= h.page_count) return "group child page out of range";

    uint32_t cluster_at = 0, tri_at = 0, vertex_at = 0;
    for (const LMeshPage& page : p_data.pages) {
        if (page.cluster_first != cluster_at || page.tri_first != tri_at || page.vertex_first != vertex_at) return "pages are not contiguous";
        if ((uint64_t)page.cluster_first + page.cluster_count > h.cluster_count || (uint64_t)page.tri_first + page.tri_count > h.tri_count || (uint64_t)page.vertex_first + page.vertex_count > h.vertex_count) return "page range out of bounds";
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
            const LMeshCluster& c = p_data.clusters[ci];
            if (c.tri_count == 0 || c.tri_count > LMESH_CLUSTER_MAX_TRIS || c.vertex_count > LMESH_CLUSTER_MAX_VERTS) return "cluster size out of range";
            if ((uint64_t)c.tri_offset + c.tri_count > page.tri_count || (uint64_t)c.vertex_offset + c.vertex_count > page.vertex_count) return "cluster range outside its page";
            if (c.self_group != CLUSTER_GROUP_NONE && c.self_group >= h.group_count) return "cluster self_group out of range";
            if (c.parent_group != CLUSTER_GROUP_NONE && c.parent_group >= h.group_count) return "cluster parent_group out of range";
            if (c.slot != LMESH_CLUSTER_SLOT_MIXED && c.slot >= h.slot_count) return "cluster slot out of range";
        }
        cluster_at += page.cluster_count;
        tri_at += page.tri_count;
        vertex_at += page.vertex_count;
    }
    if (cluster_at != h.cluster_count || tri_at != h.tri_count || vertex_at != h.vertex_count) return "pages do not cover the mesh";

    for (uint32_t i = 0; i < h.node_count; i++) {
        const LMeshNode& n = p_data.nodes[i];
        if (n.page == LMESH_NODE_INNER) {
            if (n.count == 0 || n.first <= i || (uint64_t)n.first + n.count > h.node_count) return "bad hierarchy node";
        } else {
            if (n.page >= h.page_count) return "hierarchy leaf page out of range";
            const LMeshPage& page = p_data.pages[n.page];
            if (n.first < page.cluster_first || (uint64_t)n.first + n.count > (uint64_t)page.cluster_first + page.cluster_count) return "hierarchy leaf outside its page";
        }
    }
    return nullptr;
}

const char* lmesh_validate_page(const LMeshData& p_tables, uint32_t p_page, const LMeshPageData& p_data)
{
    const LMeshPayloadHeader& h = p_tables.header;
    if (p_page >= p_tables.pages.size()) return "page out of range";
    const LMeshPage& page = p_tables.pages[p_page];
    const bool skinned = p_tables.skinned();
    const bool wide = p_tables.wide_slots();
    if (p_data.tris.size() != page.tri_count || p_data.vertices.size() != page.vertex_count) return "page data size mismatch";
    if (p_data.skin.size() != (skinned ? page.vertex_count : 0u) || p_data.tri_slots.size() != (wide ? page.tri_count : 0u)) return "page stream size mismatch";
    for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
        const LMeshCluster& c = p_tables.clusters[ci];
        for (uint32_t t = c.tri_offset; t < c.tri_offset + c.tri_count; t++) {
            const uint32_t lt = p_data.tris[t];
            if ((lt & 0xFFu) >= c.vertex_count || ((lt >> 8) & 0xFFu) >= c.vertex_count || ((lt >> 16) & 0xFFu) >= c.vertex_count) return "local index out of range";
            const uint32_t slot = lt >> 24;
            if (slot == LMESH_LOCAL_SLOT_OVERFLOW) {
                if (!wide || p_data.tri_slots[t] >= h.slot_count) return "triangle slot out of range";
            } else if (slot >= h.slot_count) {
                return "triangle slot out of range";
            }
        }
    }
    return nullptr;
}

void lmesh_page_slice(const LMeshData& p_data, uint32_t p_page, LMeshPageData& r_out)
{
    const LMeshPage& page = p_data.pages[p_page];
    r_out.tris.assign(p_data.local_tris.begin() + page.tri_first, p_data.local_tris.begin() + page.tri_first + page.tri_count);
    r_out.vertices.assign(p_data.vertices.begin() + page.vertex_first, p_data.vertices.begin() + page.vertex_first + page.vertex_count);
    r_out.skin.clear();
    r_out.tri_slots.clear();
    if (p_data.skinned()) r_out.skin.assign(p_data.skin.begin() + page.vertex_first, p_data.skin.begin() + page.vertex_first + page.vertex_count);
    if (p_data.wide_slots()) r_out.tri_slots.assign(p_data.tri_slots.begin() + page.tri_first, p_data.tri_slots.begin() + page.tri_first + page.tri_count);
}

static uint32_t _bits_for(uint32_t p_range)
{
    uint32_t bits = 0;
    while (bits < 32 && (p_range >> bits) != 0) bits++;
    return bits;
}

struct BitWriter
{
    std::vector<uint32_t>& words;
    uint64_t bit;

    BitWriter(std::vector<uint32_t>& r_words) : words(r_words), bit((uint64_t)r_words.size() * 32) {}

    void put(uint32_t p_value, uint32_t p_count)
    {
        if (p_count == 0) return;
        const size_t need = (size_t)((bit + p_count + 31) / 32);
        if (words.size() < need) words.resize(need, 0u);
        const size_t w = (size_t)(bit >> 5);
        const uint32_t s = (uint32_t)(bit & 31);
        const uint32_t v = p_count >= 32 ? p_value : p_value & ((1u << p_count) - 1u);
        words[w] |= v << s;
        if (s + p_count > 32) words[w + 1] |= v >> (32 - s);
        bit += p_count;
    }
};

static uint32_t _quantize_normal(int16_t p_snorm)
{
    const float n = glm::clamp((float)p_snorm / 32767.0f, -1.0f, 1.0f);
    const float scale = (float)((1u << LMESH_NORMAL_BITS) - 1u);
    return (uint32_t)glm::round((n * 0.5f + 0.5f) * scale);
}

void lmesh_pack_page(const LMeshData& p_tables, uint32_t p_page, const LMeshPageData& p_data, LMeshPackedPage& r_out)
{
    r_out.positions.clear();
    r_out.attributes.clear();
    r_out.skin.clear();
    r_out.clusters.clear();
    r_out.cones.clear();
    const LMeshPage& page = p_tables.pages[p_page];
    const LMeshPayloadHeader& h = p_tables.header;
    const bool skinned = p_tables.skinned() && p_data.skin.size() == p_data.vertices.size();
    r_out.clusters.resize(page.cluster_count);
    r_out.cones.assign(page.cluster_count, LMESH_CONE_NONE);
    std::vector<float> cone_points;
    std::vector<unsigned int> cone_indices;

    for (uint32_t i = 0; i < page.cluster_count; i++) {
        const LMeshCluster& c = p_tables.clusters[page.cluster_first + i];
        LMeshClusterStreams& s = r_out.clusters[i];
        const ClusterVertex* verts = p_data.vertices.data() + c.vertex_offset;

        u16vec3 lo(UINT16_MAX), hi(0);
        u16vec2 uv_lo(UINT16_MAX), uv_hi(0);
        for (uint32_t v = 0; v < c.vertex_count; v++) {
            lo = glm::min(lo, verts[v].position.position);
            hi = glm::max(hi, verts[v].position.position);
            uv_lo = glm::min(uv_lo, verts[v].attributes.uv);
            uv_hi = glm::max(uv_hi, verts[v].attributes.uv);
        }
        if (c.vertex_count == 0) {
            lo = hi = u16vec3(0);
            uv_lo = uv_hi = u16vec2(0);
        }
        const uint32_t px = _bits_for(hi.x - lo.x), py = _bits_for(hi.y - lo.y), pz = _bits_for(hi.z - lo.z);
        const uint32_t pu = _bits_for(uv_hi.x - uv_lo.x), pv = _bits_for(uv_hi.y - uv_lo.y);

        s.position = (uint32_t)r_out.positions.size();
        s.attribute = (uint32_t)r_out.attributes.size();
        s.skin = (uint32_t)r_out.skin.size();
        s.pos_lo_xy = (uint32_t)lo.x | ((uint32_t)lo.y << 16);
        s.pos_lo_z_count = (uint32_t)lo.z | (c.vertex_count << 16);
        s.uv_lo = (uint32_t)uv_lo.x | ((uint32_t)uv_lo.y << 16);
        s.bits = px | (py << 5) | (pz << 10) | (pu << 15) | (pv << 20);
        s.skin_format = 0;

        if (c.tri_count > 0 && c.vertex_count > 0 && (uint64_t)c.tri_offset + c.tri_count <= p_data.tris.size()) {
            cone_points.resize((size_t)c.vertex_count * 3);
            for (uint32_t v = 0; v < c.vertex_count; v++) {
                const vec3 pos = h.pos_min + vec3(verts[v].position.position) / 65535.0f * h.pos_extent;
                cone_points[(size_t)v * 3 + 0] = pos.x;
                cone_points[(size_t)v * 3 + 1] = pos.y;
                cone_points[(size_t)v * 3 + 2] = pos.z;
            }
            cone_indices.resize((size_t)c.tri_count * 3);
            for (uint32_t t = 0; t < c.tri_count; t++) {
                const uint32_t lt = p_data.tris[c.tri_offset + t];
                cone_indices[(size_t)t * 3 + 0] = lt & 0xFFu;
                cone_indices[(size_t)t * 3 + 1] = (lt >> 8) & 0xFFu;
                cone_indices[(size_t)t * 3 + 2] = (lt >> 16) & 0xFFu;
            }
            const meshopt_Bounds b = meshopt_computeClusterBounds(cone_indices.data(), cone_indices.size(), cone_points.data(), c.vertex_count, sizeof(float) * 3);
            if (b.cone_cutoff_s8 < 127) r_out.cones[i] = (uint32_t)(uint8_t)b.cone_axis_s8[0] | ((uint32_t)(uint8_t)b.cone_axis_s8[1] << 8) | ((uint32_t)(uint8_t)b.cone_axis_s8[2] << 16) | ((uint32_t)(uint8_t)b.cone_cutoff_s8 << 24);
        }

        BitWriter pw(r_out.positions);
        BitWriter aw(r_out.attributes);
        for (uint32_t v = 0; v < c.vertex_count; v++) {
            const u16vec3 q = verts[v].position.position;
            pw.put(q.x - lo.x, px);
            pw.put(q.y - lo.y, py);
            pw.put(q.z - lo.z, pz);
            const VertexAttributes& a = verts[v].attributes;
            aw.put(_quantize_normal(a.normal.x), LMESH_NORMAL_BITS);
            aw.put(_quantize_normal(a.normal.y), LMESH_NORMAL_BITS);
            aw.put(a.uv.x - uv_lo.x, pu);
            aw.put(a.uv.y - uv_lo.y, pv);
        }

        if (!skinned) continue;
        const SkinVertex* skin = p_data.skin.data() + c.vertex_offset;
        uint32_t influences = 1, joint_lo = 255, joint_hi = 0;
        for (uint32_t v = 0; v < c.vertex_count; v++) {
            uint32_t n = 0;
            for (int k = 0; k < 4; k++) {
                if (skin[v].weights[k] == 0) continue;
                n++;
                joint_lo = std::min<uint32_t>(joint_lo, skin[v].joints[k]);
                joint_hi = std::max<uint32_t>(joint_hi, skin[v].joints[k]);
            }
            influences = std::max(influences, n);
        }
        if (joint_lo > joint_hi) joint_lo = joint_hi = 0;
        const uint32_t joint_bits = _bits_for(joint_hi - joint_lo);
        s.skin_format = influences | (joint_bits << 3) | (joint_lo << 8);

        BitWriter sw(r_out.skin);
        for (uint32_t v = 0; v < c.vertex_count; v++) {
            uint32_t joints[4] = { joint_lo, joint_lo, joint_lo, joint_lo };
            uint32_t weights[4] = {};
            uint32_t n = 0;
            for (int k = 0; k < 4; k++) {
                if (skin[v].weights[k] == 0) continue;
                uint32_t at = n++;
                while (at > 0 && weights[at - 1] < skin[v].weights[k]) {
                    joints[at] = joints[at - 1];
                    weights[at] = weights[at - 1];
                    at--;
                }
                joints[at] = skin[v].joints[k];
                weights[at] = skin[v].weights[k];
            }
            for (uint32_t k = 0; k < influences; k++) sw.put(joints[k] - joint_lo, joint_bits);
            for (uint32_t k = 0; k + 1 < influences; k++) sw.put(weights[k], LMESH_WEIGHT_BITS);
        }
    }

    r_out.positions.resize(r_out.positions.size() + LMESH_STREAM_PAD_WORDS, 0u);
    r_out.attributes.resize(r_out.attributes.size() + LMESH_STREAM_PAD_WORDS, 0u);
    if (skinned) r_out.skin.resize(r_out.skin.size() + LMESH_STREAM_PAD_WORDS, 0u);
}

const char* lmesh_validate(const LMeshData& p_data)
{
    if (const char* why = lmesh_validate_tables(p_data)) return why;
    const LMeshPayloadHeader& h = p_data.header;
    if (!p_data.has_page_data()) return "mesh has no page data";
    if (p_data.skin.size() != (p_data.skinned() ? h.vertex_count : 0u)) return "skin stream size disagrees with the header";
    if (p_data.tri_slots.size() != (p_data.wide_slots() ? h.tri_count : 0u)) return "wide slot table size disagrees with the header";
    LMeshPageData page;
    for (uint32_t p = 0; p < h.page_count; p++) {
        lmesh_page_slice(p_data, p, page);
        if (const char* why = lmesh_validate_page(p_data, p, page)) return why;
    }
    return nullptr;
}

template <class T>
static void _append(std::vector<uint8_t>& r_out, const T* p_data, size_t p_count)
{
    if (p_count == 0) return;
    const size_t at = r_out.size();
    r_out.resize(at + p_count * sizeof(T));
    std::memcpy(r_out.data() + at, p_data, p_count * sizeof(T));
}

template <class T>
static uint32_t _encode(std::vector<uint8_t>& r_out, const T* p_data, size_t p_count)
{
    static_assert(sizeof(T) % 4 == 0 && sizeof(T) <= 256, "meshopt vertex codec stride");
    if (p_count == 0) return 0;
    const size_t at = r_out.size();
    r_out.resize(at + meshopt_encodeVertexBufferBound(p_count, sizeof(T)));
    const size_t n = meshopt_encodeVertexBufferLevel(r_out.data() + at, r_out.size() - at, p_data, p_count, sizeof(T), 3, 1);
    r_out.resize(at + n);
    return (uint32_t)n;
}

template <class T>
static bool _decode(const uint8_t* p_src, size_t p_bytes, size_t p_count, std::vector<T>& r_out)
{
    r_out.resize(p_count);
    if (p_count == 0) return p_bytes == 0;
    return p_bytes > 0 && meshopt_decodeVertexBuffer(r_out.data(), p_count, sizeof(T), p_src, p_bytes) == 0;
}

bool lmesh_write(const std::filesystem::path& p_bin, Guid p_guid, const LMeshData& p_data)
{
    const bool skinned = p_data.skinned();
    const bool wide = p_data.wide_slots();
    if (!p_data.has_page_data()) return false;

    std::string rig_names;
    for (const std::string& n : p_data.rig.joints) {
        rig_names.append(n);
        rig_names.push_back('\0');
    }
    rig_names.resize((rig_names.size() + LMESH_RIG_ALIGN - 1) / LMESH_RIG_ALIGN * LMESH_RIG_ALIGN, '\0');
    const LMeshRigHeader rig{ p_data.rig.skeleton, (uint32_t)p_data.rig.joints.size(), (uint32_t)rig_names.size() };
    const size_t rig_bytes = skinned ? lmesh_rig_bytes(rig) : 0;

    std::vector<LMeshPage> pages = p_data.pages;
    std::vector<uint8_t> blobs;
    for (LMeshPage& page : pages) {
        const size_t start = blobs.size();
        page.tri_bytes = _encode(blobs, p_data.local_tris.data() + page.tri_first, page.tri_count);
        page.vertex_bytes = _encode(blobs, p_data.vertices.data() + page.vertex_first, page.vertex_count);
        page.skin_bytes = skinned ? _encode(blobs, p_data.skin.data() + page.vertex_first, page.vertex_count) : 0u;
        if (wide) _encode(blobs, p_data.tri_slots.data() + page.tri_first, page.tri_count);
        page.size = (uint32_t)(blobs.size() - start);
        page.offset = start;
    }

    std::vector<uint8_t> resident;
    _append(resident, p_data.groups.data(), p_data.groups.size());
    _append(resident, p_data.nodes.data(), p_data.nodes.size());
    const size_t pages_at = resident.size();
    _append(resident, pages.data(), pages.size());
    _append(resident, p_data.clusters.data(), p_data.clusters.size());
    _append(resident, p_data.group_child_first.data(), p_data.group_child_first.size());
    _append(resident, p_data.group_child_pages.data(), p_data.group_child_pages.size());

    std::vector<LMeshJointBox> joint_boxes;
    if (skinned) {
        const LMeshPayloadHeader& ph = p_data.header;
        for (size_t v = 0; v < p_data.skin.size() && v < p_data.vertices.size(); v++) {
            const SkinVertex& sv = p_data.skin[v];
            const vec3 pos = ph.pos_min + vec3(p_data.vertices[v].position.position) / 65535.0f * ph.pos_extent;
            for (int k = 0; k < 4; k++) {
                if (sv.weights[k] == 0) continue;
                const uint32_t j = sv.joints[k];
                if (j >= joint_boxes.size()) joint_boxes.resize(j + 1, LMeshJointBox{ vec3(FLT_MAX), vec3(-FLT_MAX) });
                joint_boxes[j].min = glm::min(joint_boxes[j].min, pos);
                joint_boxes[j].max = glm::max(joint_boxes[j].max, pos);
            }
        }
    }
    const size_t joint_box_bytes = joint_boxes.size() * sizeof(LMeshJointBox);

    LMeshPayloadHeader header = p_data.header;
    header.section_count = 4;
    header.group_child_page_count = (uint32_t)p_data.group_child_pages.size();

    const uint64_t sections_at = sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader) + rig_bytes;
    const uint64_t resident_at = sections_at + header.section_count * sizeof(LMeshSection);
    const uint64_t blobs_at = resident_at + resident.size();
    const uint64_t joint_boxes_at = blobs_at + blobs.size();
    const uint64_t collision_at = joint_boxes_at + joint_box_bytes;
    const uint64_t total = collision_at + p_data.collision.size();
    if (total > UINT32_MAX) {
        log_write("lmesh: '%s' would be over 4 GB.", p_bin.string().c_str());
        return false;
    }
    for (LMeshPage& page : pages) page.offset += blobs_at;
    std::memcpy(resident.data() + pages_at, pages.data(), pages.size() * sizeof(LMeshPage));

    const LMeshSection sections[4] = {
        { LMeshSectionType::RASTER_RESIDENT, 0, resident_at, resident.size() },
        { LMeshSectionType::RASTER_PAGES, 0, blobs_at, blobs.size() },
        { LMeshSectionType::JOINT_BOXES, 0, joint_boxes_at, joint_box_bytes },
        { LMeshSectionType::COLLISION, 0, collision_at, p_data.collision.size() },
    };

    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LMESH_VERSION;
    ah.guid = p_guid;
    ah.type = AssetType::MESH;
    ah.payload_size = (uint32_t)(total - sizeof(LAssetHeader));

    std::vector<uint8_t> out;
    out.reserve((size_t)total);
    _append(out, &ah, 1);
    _append(out, &header, 1);
    if (skinned) {
        _append(out, &rig, 1);
        _append(out, p_data.rig.inverse_binds.data(), p_data.rig.inverse_binds.size());
        _append(out, rig_names.data(), rig_names.size());
    }
    _append(out, sections, 4);
    _append(out, resident.data(), resident.size());
    _append(out, blobs.data(), blobs.size());
    _append(out, joint_boxes.data(), joint_boxes.size());
    _append(out, p_data.collision.data(), p_data.collision.size());
    if (out.size() != total) return false;

    std::error_code ec;
    std::filesystem::create_directories(p_bin.parent_path(), ec);
    return write_file_atomic(p_bin, out.data(), out.size());
}

template <class T>
static bool _read(const std::vector<uint8_t>& p_bytes, size_t& r_at, T* r_data, size_t p_count)
{
    const size_t n = p_count * sizeof(T);
    if (r_at + n > p_bytes.size() || r_at + n < r_at) return false;
    if (n) std::memcpy(r_data, p_bytes.data() + r_at, n);
    r_at += n;
    return true;
}

static const char* _parse_rig_block(const std::vector<uint8_t>& p_bytes, size_t& r_at, LMeshRig& r_rig)
{
    if (r_at > p_bytes.size() || !lmesh_parse_rig(p_bytes.data() + r_at, p_bytes.size() - r_at, r_rig)) return "bad rig";
    LMeshRigHeader rh{};
    std::memcpy(&rh, p_bytes.data() + r_at, sizeof(rh));
    r_at += lmesh_rig_bytes(rh);
    return nullptr;
}

static bool _counts_sane(const LMeshPayloadHeader& p_h)
{
    return p_h.cluster_count <= (1u << 26) && p_h.tri_count <= (1u << 28) && p_h.vertex_count <= (1u << 28) && p_h.page_count <= (1u << 24) && p_h.node_count <= (1u << 26) && p_h.group_count <= (1u << 26) && p_h.group_child_page_count <= (1u << 28) && p_h.section_count <= 16;
}

struct LMeshPageV5
{
    uint64_t offset;
    uint32_t size;
    uint32_t cluster_first;
    uint32_t cluster_count;
    uint32_t tri_first;
    uint32_t tri_count;
    uint32_t vertex_first;
    uint32_t vertex_count;
    uint32_t level;
    uint32_t _pad[2];
};

static_assert(sizeof(LMeshPageV5) == 48, "v5 page layout is frozen");

static const char* _parse_v5(const std::vector<uint8_t>& p_bytes, LMeshData& r_data)
{
    size_t at = sizeof(LAssetHeader);
    LMeshPayloadHeader& h = r_data.header;
    if (!_read(p_bytes, at, &h, 1)) return "truncated header";
    h.section_count = 0;
    h.group_child_page_count = 0;
    if (r_data.skinned()) {
        if (const char* why = _parse_rig_block(p_bytes, at, r_data.rig)) return why;
    }
    if (!_counts_sane(h)) return "counts out of range";

    std::vector<LMeshPageV5> pages(h.page_count);
    r_data.groups.resize(h.group_count);
    r_data.nodes.resize(h.node_count);
    if (!_read(p_bytes, at, r_data.groups.data(), h.group_count) || !_read(p_bytes, at, r_data.nodes.data(), h.node_count) || !_read(p_bytes, at, pages.data(), h.page_count)) return "truncated tables";

    const bool skinned = r_data.skinned();
    const bool wide = r_data.wide_slots();
    r_data.clusters.resize(h.cluster_count);
    r_data.local_tris.resize(h.tri_count);
    r_data.vertices.resize(h.vertex_count);
    r_data.skin.resize(skinned ? h.vertex_count : 0u);
    r_data.tri_slots.resize(wide ? h.tri_count : 0u);
    r_data.pages.resize(h.page_count);
    for (uint32_t i = 0; i < h.page_count; i++) {
        const LMeshPageV5& v5 = pages[i];
        if ((uint64_t)v5.cluster_first + v5.cluster_count > h.cluster_count || (uint64_t)v5.tri_first + v5.tri_count > h.tri_count || (uint64_t)v5.vertex_first + v5.vertex_count > h.vertex_count) return "page range out of bounds";
        size_t p = (size_t)v5.offset;
        if (v5.offset > p_bytes.size()) return "page out of bounds";
        bool ok = _read(p_bytes, p, r_data.clusters.data() + v5.cluster_first, v5.cluster_count)
            && _read(p_bytes, p, r_data.local_tris.data() + v5.tri_first, v5.tri_count)
            && _read(p_bytes, p, r_data.vertices.data() + v5.vertex_first, v5.vertex_count);
        if (ok && skinned) ok = _read(p_bytes, p, r_data.skin.data() + v5.vertex_first, v5.vertex_count);
        if (ok && wide) ok = _read(p_bytes, p, r_data.tri_slots.data() + v5.tri_first, v5.tri_count);
        if (!ok) return "truncated page";
        LMeshPage& page = r_data.pages[i];
        page = {};
        page.cluster_first = v5.cluster_first;
        page.cluster_count = v5.cluster_count;
        page.tri_first = v5.tri_first;
        page.tri_count = v5.tri_count;
        page.vertex_first = v5.vertex_first;
        page.vertex_count = v5.vertex_count;
    }
    _compute_child_pages(r_data);
    return nullptr;
}

static const char* _parse_legacy(const std::vector<uint8_t>& p_bytes, uint32_t p_version, uint32_t p_payload_size, LMeshSource& r_src)
{
    const bool interleaved = p_version == LMESH_VERSION_INTERLEAVED;
    const bool legacy_slot_table = p_version < LMESH_VERSION_NO_RIG;

    size_t at = sizeof(LAssetHeader);
    LMeshLegacyHeader ph{};
    if (!_read(p_bytes, at, &ph, 1)) return "truncated header";
    if (p_payload_size < sizeof(ph)) return "payload_size underflow";
    const bool skinned = has_flag((MeshFlags)ph.flags, MeshFlags::SKINNED);

    size_t rig_bytes = 0;
    if (_has_rig_block(p_version, ph.flags)) {
        const size_t before = at;
        if (const char* why = _parse_rig_block(p_bytes, at, r_src.rig)) return why;
        rig_bytes = at - before;
    }

    const size_t vtx_bytes = (size_t)ph.vertex_count * (interleaved ? sizeof(Vertex) : sizeof(VertexPosition) + sizeof(VertexAttributes));
    const size_t idx_bytes = (size_t)ph.index_count * sizeof(uint32_t);
    const size_t tri_bytes = (size_t)ph.tri_count * sizeof(uint32_t);
    const size_t slot_bytes = legacy_slot_table ? (size_t)ph.slot_count * sizeof(uint32_t) : 0;
    const size_t clus_bytes = (size_t)ph.cluster_count * sizeof(Cluster);
    const size_t group_bytes = (size_t)ph.group_count * sizeof(ClusterGroup);
    const size_t skin_bytes = skinned ? (size_t)ph.vertex_count * sizeof(SkinVertex) : 0;
    const size_t bvhn_bytes = (size_t)ph.bvh_node_count * LEGACY_BVH_NODE_BYTES;
    if (p_payload_size < sizeof(ph) + rig_bytes) return "payload_size underflow";
    const size_t blocks = (size_t)p_payload_size - sizeof(ph) - rig_bytes;
    if (vtx_bytes + idx_bytes + tri_bytes + slot_bytes + clus_bytes + group_bytes + skin_bytes + bvhn_bytes != blocks || at + blocks > p_bytes.size()) return "bad/truncated payload";
    if (ph.vertex_count == 0 || ph.index_count == 0 || ph.index_count % 3 != 0 || ph.tri_count != ph.index_count / 3) return "bad vertex/index counts";

    r_src.positions.resize(ph.vertex_count);
    r_src.attributes.resize(ph.vertex_count);
    if (interleaved) {
        std::vector<Vertex> v(ph.vertex_count);
        _read(p_bytes, at, v.data(), v.size());
        for (uint32_t i = 0; i < ph.vertex_count; i++) {
            r_src.positions[i] = { v[i].position, 0 };
            r_src.attributes[i] = { v[i].normal, v[i].uv };
        }
    } else {
        _read(p_bytes, at, r_src.positions.data(), ph.vertex_count);
        _read(p_bytes, at, r_src.attributes.data(), ph.vertex_count);
    }
    r_src.indices.resize(ph.index_count);
    r_src.tri_slots.resize(ph.tri_count);
    r_src.clusters.resize(ph.cluster_count);
    r_src.groups.resize(ph.group_count);
    _read(p_bytes, at, r_src.indices.data(), ph.index_count);
    _read(p_bytes, at, r_src.tri_slots.data(), ph.tri_count);
    at += slot_bytes;
    _read(p_bytes, at, r_src.clusters.data(), ph.cluster_count);
    _read(p_bytes, at, r_src.groups.data(), ph.group_count);
    if (skinned) {
        r_src.skin.resize(ph.vertex_count);
        _read(p_bytes, at, r_src.skin.data(), ph.vertex_count);
    }
    r_src.slot_count = ph.slot_count;
    r_src.pos_min = ph.pos_min;
    r_src.pos_extent = ph.pos_extent;
    r_src.uv_min = ph.uv_min;
    r_src.uv_extent = ph.uv_extent;
    r_src.bounds_sphere = ph.bounds_sphere;
    return nullptr;
}

static std::vector<uint8_t> _read_range(std::ifstream& r_file, uint64_t p_offset, uint64_t p_size)
{
    std::vector<uint8_t> bytes((size_t)p_size);
    r_file.clear();
    r_file.seekg((std::streamoff)p_offset);
    if (p_size && !r_file.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)p_size)) bytes.clear();
    return bytes;
}

static const char* _open_streamed(std::ifstream& r_file, uint64_t p_file_size, LMeshData& r_data)
{
    const std::vector<uint8_t> head = _read_range(r_file, 0, std::min<uint64_t>(p_file_size, sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader) + sizeof(LMeshRigHeader)));
    size_t at = sizeof(LAssetHeader);
    LMeshPayloadHeader& h = r_data.header;
    if (!_read(head, at, &h, 1)) return "truncated header";
    if (!_counts_sane(h)) return "counts out of range";

    if (r_data.skinned()) {
        LMeshRigHeader rh{};
        if (!_read(head, at, &rh, 1) || rh.joint_count > MESH_MAX_JOINTS) return "bad rig";
        const std::vector<uint8_t> rig = _read_range(r_file, at - sizeof(rh), lmesh_rig_bytes(rh));
        if (rig.empty() || !lmesh_parse_rig(rig.data(), rig.size(), r_data.rig)) return "bad rig";
        at = at - sizeof(rh) + rig.size();
    }

    const std::vector<uint8_t> table = _read_range(r_file, at, (uint64_t)h.section_count * sizeof(LMeshSection));
    r_data.sections.resize(h.section_count);
    size_t t = 0;
    if (!_read(table, t, r_data.sections.data(), h.section_count)) return "truncated section table";

    const LMeshSection* resident = nullptr;
    const LMeshSection* joint_boxes = nullptr;
    for (const LMeshSection& s : r_data.sections) {
        if (s.offset + s.size > p_file_size || s.offset + s.size < s.offset) return "section out of bounds";
        if (s.type == LMeshSectionType::RASTER_RESIDENT) resident = &s;
        if (s.type == LMeshSectionType::JOINT_BOXES) joint_boxes = &s;
    }
    if (!resident) return "no raster section";
    if (joint_boxes && joint_boxes->size) {
        if (joint_boxes->size % sizeof(LMeshJointBox) || joint_boxes->size / sizeof(LMeshJointBox) > MESH_MAX_JOINTS) return "bad joint boxes";
        const std::vector<uint8_t> boxes = _read_range(r_file, joint_boxes->offset, joint_boxes->size);
        if (boxes.size() != joint_boxes->size) return "truncated joint boxes";
        r_data.joint_boxes.resize(boxes.size() / sizeof(LMeshJointBox));
        std::memcpy(r_data.joint_boxes.data(), boxes.data(), boxes.size());
    }

    const std::vector<uint8_t> block = _read_range(r_file, resident->offset, resident->size);
    if (block.size() != resident->size) return "truncated raster section";
    size_t b = 0;
    r_data.groups.resize(h.group_count);
    r_data.nodes.resize(h.node_count);
    r_data.pages.resize(h.page_count);
    r_data.clusters.resize(h.cluster_count);
    r_data.group_child_first.resize((size_t)h.group_count + 1);
    r_data.group_child_pages.resize(h.group_child_page_count);
    const bool ok = _read(block, b, r_data.groups.data(), r_data.groups.size())
        && _read(block, b, r_data.nodes.data(), r_data.nodes.size())
        && _read(block, b, r_data.pages.data(), r_data.pages.size())
        && _read(block, b, r_data.clusters.data(), r_data.clusters.size())
        && _read(block, b, r_data.group_child_first.data(), r_data.group_child_first.size())
        && _read(block, b, r_data.group_child_pages.data(), r_data.group_child_pages.size());
    if (!ok) return "truncated raster section";

    for (const LMeshPage& page : r_data.pages) {
        if (page.offset + page.size > p_file_size) return "page out of bounds";
        if ((uint64_t)page.tri_bytes + page.vertex_bytes + page.skin_bytes > page.size) return "page streams out of bounds";
    }
    return lmesh_validate_tables(r_data);
}

const char* lmesh_decode_page(const uint8_t* p_blob, size_t p_size, const LMeshPage& p_page, uint32_t p_flags, LMeshPageData& r_out)
{
    const bool skinned = has_flag((MeshFlags)p_flags, MeshFlags::SKINNED);
    const bool wide = has_flag((MeshFlags)p_flags, MeshFlags::WIDE_SLOTS);
    if (p_size != p_page.size) return "page size mismatch";
    const uint64_t used = (uint64_t)p_page.tri_bytes + p_page.vertex_bytes + p_page.skin_bytes;
    if (used > p_size) return "page streams out of bounds";
    const size_t slot_bytes = p_size - (size_t)used;
    if ((!skinned && p_page.skin_bytes) || (!wide && slot_bytes)) return "unexpected page stream";

    const uint8_t* p = p_blob;
    if (!_decode(p, p_page.tri_bytes, p_page.tri_count, r_out.tris)) return "bad triangle stream";
    p += p_page.tri_bytes;
    if (!_decode(p, p_page.vertex_bytes, p_page.vertex_count, r_out.vertices)) return "bad vertex stream";
    p += p_page.vertex_bytes;
    if (skinned) {
        if (!_decode(p, p_page.skin_bytes, p_page.vertex_count, r_out.skin)) return "bad skin stream";
        p += p_page.skin_bytes;
    } else {
        r_out.skin.clear();
    }
    if (wide) {
        if (!_decode(p, slot_bytes, p_page.tri_count, r_out.tri_slots)) return "bad slot stream";
    } else {
        r_out.tri_slots.clear();
    }
    return nullptr;
}

bool lmesh_open(const std::filesystem::path& p_bin, LMeshData& r_tables, uint32_t& r_version, std::string& r_error)
{
    r_tables = {};
    r_version = 0;
    std::ifstream f(p_bin, std::ios::binary | std::ios::ate);
    if (!f) { r_error = "cannot open"; return false; }
    const uint64_t file_size = (uint64_t)f.tellg();
    LAssetHeader ah{};
    f.seekg(0);
    if (file_size < sizeof(ah) || !f.read(reinterpret_cast<char*>(&ah), sizeof(ah))) { r_error = "too small"; return false; }
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH) { r_error = "bad asset header"; return false; }
    if (!asset_version_supported(AssetType::MESH, ah.version)) { r_error = "unsupported mesh version"; return false; }
    r_version = ah.version;
    if (ah.version < LMESH_VERSION_STREAMED) {
        f.close();
        return lmesh_load(p_bin, r_tables, r_error);
    }
    if (const char* why = _open_streamed(f, file_size, r_tables)) {
        r_error = why;
        r_tables = {};
        return false;
    }
    return true;
}

bool lmesh_load(const std::filesystem::path& p_bin, LMeshData& r_data, std::string& r_error)
{
    r_data = {};
    std::ifstream f(p_bin, std::ios::binary | std::ios::ate);
    if (!f) { r_error = "cannot open"; return false; }
    const uint64_t file_size = (uint64_t)f.tellg();
    LAssetHeader ah{};
    f.seekg(0);
    if (file_size < sizeof(ah) || !f.read(reinterpret_cast<char*>(&ah), sizeof(ah))) { r_error = "too small"; return false; }
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH) { r_error = "bad asset header"; return false; }
    if (!asset_version_supported(AssetType::MESH, ah.version)) { r_error = "unsupported mesh version"; return false; }
    if ((uint64_t)ah.payload_size + sizeof(ah) > file_size) { r_error = "truncated payload"; return false; }

    const char* why = nullptr;
    if (ah.version >= LMESH_VERSION_STREAMED) {
        why = _open_streamed(f, file_size, r_data);
        if (!why) {
            const LMeshPayloadHeader& h = r_data.header;
            r_data.local_tris.resize(h.tri_count);
            r_data.vertices.resize(h.vertex_count);
            r_data.skin.resize(r_data.skinned() ? h.vertex_count : 0u);
            r_data.tri_slots.resize(r_data.wide_slots() ? h.tri_count : 0u);
            LMeshPageData page;
            for (uint32_t i = 0; i < h.page_count && !why; i++) {
                const LMeshPage& info = r_data.pages[i];
                const std::vector<uint8_t> blob = _read_range(f, info.offset, info.size);
                if (blob.size() != info.size) { why = "truncated page"; break; }
                why = lmesh_decode_page(blob.data(), blob.size(), info, h.flags, page);
                if (why) break;
                std::copy(page.tris.begin(), page.tris.end(), r_data.local_tris.begin() + info.tri_first);
                std::copy(page.vertices.begin(), page.vertices.end(), r_data.vertices.begin() + info.vertex_first);
                if (r_data.skinned()) std::copy(page.skin.begin(), page.skin.end(), r_data.skin.begin() + info.vertex_first);
                if (r_data.wide_slots()) std::copy(page.tri_slots.begin(), page.tri_slots.end(), r_data.tri_slots.begin() + info.tri_first);
            }
            if (!why) why = lmesh_validate(r_data);
        }
    } else {
        f.close();
        std::ifstream all(p_bin, std::ios::binary);
        std::vector<uint8_t> bytes((size_t)file_size);
        if (!all.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)file_size)) { r_error = "read failed"; return false; }
        if (ah.version == LMESH_VERSION_PAGED) {
            why = _parse_v5(bytes, r_data);
            if (!why) why = lmesh_validate(r_data);
        } else {
            LMeshSource src;
            why = _parse_legacy(bytes, ah.version, ah.payload_size, src);
            if (!why) why = lmesh_build(src, r_data);
        }
    }
    if (why) {
        r_error = why;
        r_data = {};
        return false;
    }
    return true;
}

bool lmesh_read_stats(const std::filesystem::path& p_bin, LMeshStats& r_stats)
{
    r_stats = {};
    std::error_code ec;
    r_stats.disk_size = std::filesystem::file_size(p_bin, ec);
    if (ec) return false;

    std::ifstream f(p_bin, std::ios::binary);
    LAssetHeader ah{};
    if (!f.read(reinterpret_cast<char*>(&ah), sizeof(ah))) return false;
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH) return false;

    if (ah.version >= LMESH_VERSION_PAGED) {
        LMeshPayloadHeader h{};
        if (!f.read(reinterpret_cast<char*>(&h), sizeof(h))) return false;
        if (has_flag((MeshFlags)h.flags, MeshFlags::SKINNED)) {
            LMeshRigHeader rh{};
            if (f.read(reinterpret_cast<char*>(&rh), sizeof(rh))) r_stats.joints = rh.joint_count;
        }
        r_stats.vertices = h.source_vertex_count;
        r_stats.triangles = h.lod0_tri_count;
        r_stats.slots = h.slot_count;
        r_stats.clusters = h.cluster_count;
        r_stats.cluster_vertices = h.vertex_count;
        r_stats.cluster_triangles = h.tri_count;
        r_stats.lods = h.level_count;
        r_stats.size = h.pos_extent;
        return true;
    }

    LMeshLegacyHeader ph{};
    if (!f.read(reinterpret_cast<char*>(&ph), sizeof(ph))) return false;
    if (_has_rig_block(ah.version, ph.flags)) {
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

static bool _seek_rig(std::fstream& r_file)
{
    LAssetHeader ah{};
    if (!r_file.read(reinterpret_cast<char*>(&ah), sizeof(ah))) return false;
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH || ah.version < LMESH_VERSION_RIG) return false;
    uint8_t header[sizeof(LMeshPayloadHeader)] = {};
    const size_t header_bytes = _payload_header_bytes(ah.version);
    if (!r_file.read(reinterpret_cast<char*>(header), (std::streamsize)header_bytes)) return false;
    return _has_rig_block(ah.version, _payload_flags(ah.version, header));
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
    LMeshRigHeader rh{};
    if (!_seek_rig(f) || !f.read(reinterpret_cast<char*>(&rh), sizeof(rh)) || rh.joint_count > MESH_MAX_JOINTS) return false;

    std::vector<uint8_t> block(lmesh_rig_bytes(rh));
    std::memcpy(block.data(), &rh, sizeof(rh));
    if (!f.read(reinterpret_cast<char*>(block.data() + sizeof(rh)), (std::streamsize)(block.size() - sizeof(rh)))) return false;
    return lmesh_parse_rig(block.data(), block.size(), r_rig);
}

bool lmesh_set_skeleton(const std::filesystem::path& p_bin, Guid p_skeleton)
{
    std::fstream f(p_bin, std::ios::in | std::ios::out | std::ios::binary);
    if (!_seek_rig(f)) return false;
    f.seekp(f.tellg());
    f.write(reinterpret_cast<const char*>(&p_skeleton), sizeof(Guid));
    return (bool)f;
}

static bool _section_table(std::ifstream& r_file, uint64_t p_file_size, uint64_t& r_table_at, LMeshPayloadHeader& r_header)
{
    const std::vector<uint8_t> head = _read_range(r_file, 0, std::min<uint64_t>(p_file_size, sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader) + sizeof(LMeshRigHeader)));
    if (head.size() < sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader)) return false;
    LAssetHeader ah;
    std::memcpy(&ah, head.data(), sizeof(ah));
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH || ah.version != LMESH_VERSION) return false;
    std::memcpy(&r_header, head.data() + sizeof(ah), sizeof(r_header));
    if (r_header.section_count > 16) return false;
    uint64_t at = sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader);
    if (r_header.flags & (uint32_t)MeshFlags::SKINNED) {
        if (head.size() < at + sizeof(LMeshRigHeader)) return false;
        LMeshRigHeader rh;
        std::memcpy(&rh, head.data() + at, sizeof(rh));
        if (rh.joint_count > MESH_MAX_JOINTS) return false;
        at += lmesh_rig_bytes(rh);
    }
    if (at + (uint64_t)r_header.section_count * sizeof(LMeshSection) > p_file_size) return false;
    r_table_at = at;
    return true;
}

static int _collision_entry(std::ifstream& r_file, uint64_t p_file_size, uint64_t& r_table_at, LMeshSection& r_section)
{
    LMeshPayloadHeader h{};
    if (!_section_table(r_file, p_file_size, r_table_at, h)) return -1;
    const std::vector<uint8_t> table = _read_range(r_file, r_table_at, (uint64_t)h.section_count * sizeof(LMeshSection));
    if (table.size() != (size_t)h.section_count * sizeof(LMeshSection)) return -1;
    for (uint32_t i = 0; i < h.section_count; i++) {
        std::memcpy(&r_section, table.data() + (size_t)i * sizeof(LMeshSection), sizeof(LMeshSection));
        if (r_section.type == LMeshSectionType::COLLISION) return (int)i;
    }
    return -1;
}

bool lmesh_read_collision(const std::filesystem::path& p_bin, std::vector<uint8_t>& r_collision)
{
    r_collision.clear();
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(p_bin, ec);
    if (ec) return false;
    std::ifstream f(p_bin, std::ios::binary);
    if (!f) return false;
    uint64_t table_at = 0;
    LMeshSection section{};
    if (_collision_entry(f, size, table_at, section) < 0 || section.size == 0 || section.offset + section.size > size) return false;
    r_collision = _read_range(f, section.offset, section.size);
    return r_collision.size() == section.size;
}

bool lmesh_has_collision_slot(const std::filesystem::path& p_bin)
{
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(p_bin, ec);
    if (ec) return false;
    std::ifstream f(p_bin, std::ios::binary);
    if (!f) return false;
    uint64_t table_at = 0;
    LMeshSection section{};
    return _collision_entry(f, size, table_at, section) >= 0;
}

LMeshCollisionWrite lmesh_write_collision(const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const std::vector<uint8_t>& p_collision)
{
    using enum LMeshCollisionWrite;
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(p_src, ec);
    if (ec) return FAILED;
    std::ifstream f(p_src, std::ios::binary);
    if (!f) return FAILED;
    uint64_t table_at = 0;
    LMeshSection section{};
    const int index = _collision_entry(f, size, table_at, section);
    if (index < 0) return NO_SLOT;
    if (section.offset + section.size != size) return FAILED;

    std::vector<uint8_t> bytes = _read_range(f, 0, section.offset);
    f.close();
    if (bytes.size() != section.offset) return FAILED;
    if (bytes.size() + p_collision.size() > UINT32_MAX) return FAILED;
    bytes.insert(bytes.end(), p_collision.begin(), p_collision.end());

    section.size = p_collision.size();
    std::memcpy(bytes.data() + table_at + (size_t)index * sizeof(LMeshSection), &section, sizeof(section));
    LAssetHeader ah;
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    ah.payload_size = (uint32_t)(bytes.size() - sizeof(LAssetHeader));
    std::memcpy(bytes.data(), &ah, sizeof(ah));

    std::filesystem::create_directories(p_dst.parent_path(), ec);
    return write_file_atomic(p_dst, bytes.data(), bytes.size()) ? OK : FAILED;
}

}
