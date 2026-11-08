#include <core/assets/lmesh.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
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

static size_t _page_bytes(const LMeshPage& p_page, bool p_skinned, bool p_wide)
{
    size_t bytes = (size_t)p_page.cluster_count * sizeof(LMeshCluster);
    bytes += (size_t)p_page.tri_count * sizeof(uint32_t);
    bytes += (size_t)p_page.vertex_count * sizeof(ClusterVertex);
    if (p_skinned) bytes += (size_t)p_page.vertex_count * sizeof(SkinVertex);
    if (p_wide) bytes += (size_t)p_page.tri_count * sizeof(uint32_t);
    return bytes;
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

    // Parts: clusters made from the same group (self_group) that refine into the same
    // parent group. A part is the smallest unit a streamer swaps; a unit (all parts of one
    // self_group) never splits across pages.
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
            page.level = std::max(page.level, part.level);
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
        page.size = (uint32_t)_page_bytes(page, skinned, wide);
        r_data.pages.push_back(page);
    }

    // Hierarchy: one bottom-up tree per (root, level) run of parts, then a tree over those
    // roots, laid out breadth-first so the root is node 0 and siblings are contiguous.
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
    return lmesh_validate(r_data);
}

const char* lmesh_validate(const LMeshData& p_data)
{
    const LMeshPayloadHeader& h = p_data.header;
    const bool skinned = p_data.skinned();
    const bool wide = p_data.wide_slots();
    if (h.slot_count == 0) return "mesh has no material slots";
    if (h.cluster_count == 0 || h.page_count == 0 || h.node_count == 0) return "mesh has no clusters";
    if (p_data.clusters.size() != h.cluster_count || p_data.local_tris.size() != h.tri_count || p_data.vertices.size() != h.vertex_count) return "cluster array sizes disagree with the header";
    if (p_data.groups.size() != h.group_count || p_data.nodes.size() != h.node_count || p_data.pages.size() != h.page_count) return "table sizes disagree with the header";
    if (p_data.skin.size() != (skinned ? h.vertex_count : 0u)) return "skin stream size disagrees with the header";
    if (p_data.tri_slots.size() != (wide ? h.tri_count : 0u)) return "wide slot table size disagrees with the header";
    if (h.root_page_count == 0 || h.root_page_count > h.page_count) return "bad root page count";

    uint32_t cluster_at = 0, tri_at = 0, vertex_at = 0;
    for (const LMeshPage& page : p_data.pages) {
        if (page.cluster_first != cluster_at || page.tri_first != tri_at || page.vertex_first != vertex_at) return "pages are not contiguous";
        if ((uint64_t)page.cluster_first + page.cluster_count > h.cluster_count || (uint64_t)page.tri_first + page.tri_count > h.tri_count || (uint64_t)page.vertex_first + page.vertex_count > h.vertex_count) return "page range out of bounds";
        if (page.size != _page_bytes(page, skinned, wide)) return "page size mismatch";
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count; ci++) {
            const LMeshCluster& c = p_data.clusters[ci];
            if (c.tri_count == 0 || c.tri_count > LMESH_CLUSTER_MAX_TRIS || c.vertex_count > LMESH_CLUSTER_MAX_VERTS) return "cluster size out of range";
            if ((uint64_t)c.tri_offset + c.tri_count > page.tri_count || (uint64_t)c.vertex_offset + c.vertex_count > page.vertex_count) return "cluster range outside its page";
            if (c.self_group != CLUSTER_GROUP_NONE && c.self_group >= h.group_count) return "cluster self_group out of range";
            if (c.parent_group != CLUSTER_GROUP_NONE && c.parent_group >= h.group_count) return "cluster parent_group out of range";
            if (c.slot != LMESH_CLUSTER_SLOT_MIXED && c.slot >= h.slot_count) return "cluster slot out of range";
            const uint32_t first = page.tri_first + c.tri_offset;
            for (uint32_t t = first; t < first + c.tri_count; t++) {
                const uint32_t lt = p_data.local_tris[t];
                if ((lt & 0xFFu) >= c.vertex_count || ((lt >> 8) & 0xFFu) >= c.vertex_count || ((lt >> 16) & 0xFFu) >= c.vertex_count) return "local index out of range";
                const uint32_t slot = lt >> 24;
                if (slot == LMESH_LOCAL_SLOT_OVERFLOW) {
                    if (!wide || p_data.tri_slots[t] >= h.slot_count) return "triangle slot out of range";
                } else if (slot >= h.slot_count) {
                    return "triangle slot out of range";
                }
            }
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

template <class T>
static void _append(std::vector<uint8_t>& r_out, const T* p_data, size_t p_count)
{
    if (p_count == 0) return;
    const size_t at = r_out.size();
    r_out.resize(at + p_count * sizeof(T));
    std::memcpy(r_out.data() + at, p_data, p_count * sizeof(T));
}

bool lmesh_write(const std::filesystem::path& p_bin, Guid p_guid, const LMeshData& p_data)
{
    const bool skinned = p_data.skinned();
    const bool wide = p_data.wide_slots();

    std::string rig_names;
    for (const std::string& n : p_data.rig.joints) {
        rig_names.append(n);
        rig_names.push_back('\0');
    }
    rig_names.resize((rig_names.size() + LMESH_RIG_ALIGN - 1) / LMESH_RIG_ALIGN * LMESH_RIG_ALIGN, '\0');
    const LMeshRigHeader rig{ p_data.rig.skeleton, (uint32_t)p_data.rig.joints.size(), (uint32_t)rig_names.size() };
    const size_t rig_bytes = skinned ? lmesh_rig_bytes(rig) : 0;

    std::vector<LMeshPage> pages = p_data.pages;
    uint64_t at = sizeof(LAssetHeader) + sizeof(LMeshPayloadHeader) + rig_bytes;
    at += p_data.groups.size() * sizeof(ClusterGroup) + p_data.nodes.size() * sizeof(LMeshNode) + pages.size() * sizeof(LMeshPage);
    for (LMeshPage& page : pages) {
        page.size = (uint32_t)_page_bytes(page, skinned, wide);
        page.offset = at;
        at += page.size;
    }
    if (at > UINT32_MAX) {
        log_write("lmesh: '%s' would be over 4 GB.", p_bin.string().c_str());
        return false;
    }

    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LMESH_VERSION;
    ah.guid = p_guid;
    ah.type = AssetType::MESH;
    ah.payload_size = (uint32_t)(at - sizeof(LAssetHeader));

    std::vector<uint8_t> out;
    out.reserve((size_t)at);
    _append(out, &ah, 1);
    _append(out, &p_data.header, 1);
    if (skinned) {
        _append(out, &rig, 1);
        _append(out, p_data.rig.inverse_binds.data(), p_data.rig.inverse_binds.size());
        _append(out, rig_names.data(), rig_names.size());
    }
    _append(out, p_data.groups.data(), p_data.groups.size());
    _append(out, p_data.nodes.data(), p_data.nodes.size());
    _append(out, pages.data(), pages.size());
    for (const LMeshPage& page : pages) {
        _append(out, p_data.clusters.data() + page.cluster_first, page.cluster_count);
        _append(out, p_data.local_tris.data() + page.tri_first, page.tri_count);
        _append(out, p_data.vertices.data() + page.vertex_first, page.vertex_count);
        if (skinned) _append(out, p_data.skin.data() + page.vertex_first, page.vertex_count);
        if (wide) _append(out, p_data.tri_slots.data() + page.tri_first, page.tri_count);
    }
    if (out.size() != at) return false;

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

static const char* _parse_paged(const std::vector<uint8_t>& p_bytes, LMeshData& r_data)
{
    size_t at = sizeof(LAssetHeader);
    LMeshPayloadHeader& h = r_data.header;
    if (!_read(p_bytes, at, &h, 1)) return "truncated header";
    if (r_data.skinned()) {
        if (const char* why = _parse_rig_block(p_bytes, at, r_data.rig)) return why;
    }
    if (h.cluster_count > (1u << 26) || h.tri_count > (1u << 28) || h.vertex_count > (1u << 28) || h.page_count > (1u << 24) || h.node_count > (1u << 26) || h.group_count > (1u << 26)) return "counts out of range";

    r_data.groups.resize(h.group_count);
    r_data.nodes.resize(h.node_count);
    r_data.pages.resize(h.page_count);
    if (!_read(p_bytes, at, r_data.groups.data(), h.group_count) || !_read(p_bytes, at, r_data.nodes.data(), h.node_count) || !_read(p_bytes, at, r_data.pages.data(), h.page_count)) return "truncated tables";

    const bool skinned = r_data.skinned();
    const bool wide = r_data.wide_slots();
    r_data.clusters.resize(h.cluster_count);
    r_data.local_tris.resize(h.tri_count);
    r_data.vertices.resize(h.vertex_count);
    r_data.skin.resize(skinned ? h.vertex_count : 0u);
    r_data.tri_slots.resize(wide ? h.tri_count : 0u);
    for (const LMeshPage& page : r_data.pages) {
        if ((uint64_t)page.cluster_first + page.cluster_count > h.cluster_count || (uint64_t)page.tri_first + page.tri_count > h.tri_count || (uint64_t)page.vertex_first + page.vertex_count > h.vertex_count) return "page range out of bounds";
        if (page.size != _page_bytes(page, skinned, wide) || page.offset + page.size > p_bytes.size()) return "page out of bounds";
        size_t p = (size_t)page.offset;
        bool ok = _read(p_bytes, p, r_data.clusters.data() + page.cluster_first, page.cluster_count)
            && _read(p_bytes, p, r_data.local_tris.data() + page.tri_first, page.tri_count)
            && _read(p_bytes, p, r_data.vertices.data() + page.vertex_first, page.vertex_count);
        if (ok && skinned) ok = _read(p_bytes, p, r_data.skin.data() + page.vertex_first, page.vertex_count);
        if (ok && wide) ok = _read(p_bytes, p, r_data.tri_slots.data() + page.tri_first, page.tri_count);
        if (!ok) return "truncated page";
    }
    return lmesh_validate(r_data);
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

bool lmesh_load(const std::filesystem::path& p_bin, LMeshData& r_data, std::string& r_error)
{
    r_data = {};
    std::ifstream f(p_bin, std::ios::binary | std::ios::ate);
    if (!f) { r_error = "cannot open"; return false; }
    const std::streamsize file_size = f.tellg();
    f.seekg(0);
    if (file_size < (std::streamsize)sizeof(LAssetHeader)) { r_error = "too small"; return false; }
    std::vector<uint8_t> bytes((size_t)file_size);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), file_size)) { r_error = "read failed"; return false; }

    LAssetHeader ah{};
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::MESH) { r_error = "bad asset header"; return false; }
    if (!asset_version_supported(AssetType::MESH, ah.version)) { r_error = "unsupported mesh version"; return false; }
    if ((uint64_t)ah.payload_size + sizeof(ah) > bytes.size()) { r_error = "truncated payload"; return false; }

    const char* why = nullptr;
    if (ah.version >= LMESH_VERSION_PAGED) {
        why = _parse_paged(bytes, r_data);
    } else {
        LMeshSource src;
        why = _parse_legacy(bytes, ah.version, ah.payload_size, src);
        if (!why) why = lmesh_build(src, r_data);
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

}
