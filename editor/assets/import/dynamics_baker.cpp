#include <editor/assets/import/dynamics_baker.h>
#include <core/base/error.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <meshoptimizer.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

namespace lumen {

static constexpr float CLOTH_WELD_STEP = 1e-5f;
static constexpr uint32_t CLOTH_MIN_PARTICLES = 6;
static constexpr int CLOTH_SIMPLIFY_ATTEMPTS = 10;
static constexpr float CLOTH_SIMPLIFY_SHRINK = 0.7f;
static constexpr float CLOTH_PINNED = 0.05f;
static constexpr float CLOTH_LOD_EDGE_ERROR = 0.5f;

static vec3 _closest_on_triangle(const vec3& p_p, const vec3& p_a, const vec3& p_b, const vec3& p_c, vec3& r_bary)
{
    const vec3 ab = p_b - p_a, ac = p_c - p_a, ap = p_p - p_a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { r_bary = vec3(1.0f, 0.0f, 0.0f); return p_a; }
    const vec3 bp = p_p - p_b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { r_bary = vec3(0.0f, 1.0f, 0.0f); return p_b; }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        r_bary = vec3(1.0f - v, v, 0.0f);
        return p_a + ab * v;
    }
    const vec3 cp = p_p - p_c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { r_bary = vec3(0.0f, 0.0f, 1.0f); return p_c; }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        r_bary = vec3(1.0f - w, 0.0f, w);
        return p_a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        r_bary = vec3(0.0f, 1.0f - w, w);
        return p_b + (p_c - p_b) * w;
    }
    const float denom = va + vb + vc;
    if (std::abs(denom) < 1e-20f) { r_bary = vec3(1.0f, 0.0f, 0.0f); return p_a; }
    const float v = vb / denom;
    const float w = vc / denom;
    r_bary = vec3(1.0f - v - w, v, w);
    return p_a + ab * v + ac * w;
}

static u8vec4 _quantize_weights(vec3 p_w)
{
    p_w = glm::max(p_w, vec3(0.0f));
    const float sum = p_w.x + p_w.y + p_w.z;
    p_w = sum > 0.0f ? p_w / sum : vec3(1.0f, 0.0f, 0.0f);
    int q[3], total = 0, top = 0;
    for (int k = 0; k < 3; k++) {
        q[k] = (int)std::round(p_w[k] * 255.0f);
        total += q[k];
        if (q[k] > q[top]) top = k;
    }
    q[top] += 255 - total;
    return u8vec4((uint8_t)q[0], (uint8_t)q[1], (uint8_t)q[2], 0u);
}

static uint64_t _weld_key(const vec3& p_p)
{
    const int64_t x = (int64_t)std::llround(p_p.x / CLOTH_WELD_STEP);
    const int64_t y = (int64_t)std::llround(p_p.y / CLOTH_WELD_STEP);
    const int64_t z = (int64_t)std::llround(p_p.z / CLOTH_WELD_STEP);
    uint64_t h = 1469598103934665603ull;
    for (int64_t c : { x, y, z }) h = (h ^ (uint64_t)c) * 1099511628211ull;
    return h;
}

struct ClothRegion
{
    std::string label;
    uint32_t slot = SKELETON_CLOTH_NO_SLOT;
    std::vector<uint8_t> vertex;
    std::vector<uint8_t> tri;
    std::vector<float> free;
};

static uint32_t _tri_slot(const LMeshData& p_data, size_t p_at)
{
    if (p_data.wide_slots() && p_at < p_data.tri_slots.size()) return p_data.tri_slots[p_at];
    return p_data.local_tris[p_at] >> 24;
}

template <class F>
static void _each_triangle(const LMeshData& p_data, bool p_finest_only, F&& p_fn)
{
    for (const LMeshPage& page : p_data.pages) {
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count && ci < p_data.clusters.size(); ci++) {
            const LMeshCluster& c = p_data.clusters[ci];
            if (p_finest_only && c.self_group < p_data.groups.size() && p_data.groups[c.self_group].level != 0) continue;
            const uint32_t base = page.vertex_first + c.vertex_offset;
            for (uint32_t t = 0; t < c.tri_count; t++) {
                const size_t at = (size_t)page.tri_first + c.tri_offset + t;
                if (at >= p_data.local_tris.size()) break;
                const uint32_t lt = p_data.local_tris[at];
                p_fn(base + (lt & 0xFFu), base + ((lt >> 8) & 0xFFu), base + ((lt >> 16) & 0xFFu), _tri_slot(p_data, at));
            }
        }
    }
}

static bool _slot_region(const SkeletonCloth& p_def, const LMeshData& p_data, const std::vector<uint32_t>& p_tri_slots, ClothRegion& r_region)
{
    r_region.label = "material slot " + std::to_string(p_def.slot);
    r_region.slot = p_def.slot;
    const size_t vertex_count = p_data.vertices.size();
    if (p_data.pins.size() != vertex_count) {
        log_write("MeshCooker: cloth on %s skipped, the mesh has no vertex colours to read pinning from. Paint black (pinned) to white (free) in the first colour layer and reimport.", r_region.label.c_str());
        return false;
    }
    r_region.vertex.assign(vertex_count, 0);
    _each_triangle(p_data, false, [&](uint32_t p_a, uint32_t p_b, uint32_t p_c, uint32_t p_slot) {
        if (p_slot != p_def.slot || p_a >= vertex_count || p_b >= vertex_count || p_c >= vertex_count) return;
        r_region.vertex[p_a] = r_region.vertex[p_b] = r_region.vertex[p_c] = 1;
    });
    r_region.tri.resize(p_tri_slots.size());
    for (size_t t = 0; t < p_tri_slots.size(); t++) r_region.tri[t] = p_tri_slots[t] == p_def.slot ? 1 : 0;
    r_region.free.resize(vertex_count);
    for (size_t v = 0; v < vertex_count; v++) r_region.free[v] = (float)p_data.pins[v] / 255.0f;
    return true;
}

static bool _bake_cloth(uint32_t p_index, const SkeletonCloth& p_def, const ClothRegion& p_region, const std::vector<vec3>& p_positions, const std::vector<uint32_t>& p_indices, const std::vector<SkinVertex>& p_original, std::vector<SkinVertex>& r_skin, std::vector<std::string>& r_joints, std::vector<mat4>& r_inverse_binds, LMeshDynamics& r_out)
{
    const std::string& root_name = p_region.label;
    const size_t vertex_count = std::min({ p_positions.size(), p_original.size(), p_region.vertex.size() });

    std::unordered_map<uint64_t, uint32_t> weld;
    std::vector<uint32_t> weld_of(vertex_count, UINT32_MAX);
    std::vector<vec3> wpos;
    std::vector<uint32_t> wsrc;
    for (size_t v = 0; v < vertex_count; v++) {
        if (!p_region.vertex[v]) continue;
        auto [it, added] = weld.try_emplace(_weld_key(p_positions[v]), (uint32_t)wpos.size());
        if (added) {
            wpos.push_back(p_positions[v]);
            wsrc.push_back((uint32_t)v);
        }
        weld_of[v] = it->second;
    }

    std::vector<uint32_t> wtris;
    for (size_t t = 0; t + 2 < p_indices.size(); t += 3) {
        if (t / 3 >= p_region.tri.size() || !p_region.tri[t / 3]) continue;
        const uint32_t a = p_indices[t], b = p_indices[t + 1], c = p_indices[t + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;
        const uint32_t wa = weld_of[a], wb = weld_of[b], wc = weld_of[c];
        if (wa == UINT32_MAX || wb == UINT32_MAX || wc == UINT32_MAX || wa == wb || wb == wc || wa == wc) continue;
        wtris.insert(wtris.end(), { wa, wb, wc });
    }
    if (wtris.empty()) {
        log_write("MeshCooker: cloth on '%s' has no triangles, skipped.", root_name.c_str());
        return false;
    }

    const uint32_t budget = r_joints.size() < MESH_MAX_JOINTS ? MESH_MAX_JOINTS - (uint32_t)r_joints.size() : 0u;
    const uint32_t want = std::min(p_def.resolution, budget);
    if (want < CLOTH_MIN_PARTICLES) {
        log_write("MeshCooker: cloth on '%s' skipped, the mesh already uses %zu of %u joints.", root_name.c_str(), r_joints.size(), MESH_MAX_JOINTS);
        return false;
    }

    std::vector<uint32_t> sim = wtris;
    auto unique_count = [](const std::vector<uint32_t>& p_idx) {
        std::vector<uint32_t> u = p_idx;
        std::sort(u.begin(), u.end());
        return (uint32_t)(std::unique(u.begin(), u.end()) - u.begin());
    };
    size_t target = std::min(wtris.size(), (size_t)want * 6u);
    for (int attempt = 0; attempt < CLOTH_SIMPLIFY_ATTEMPTS && unique_count(sim) > want; attempt++) {
        std::vector<uint32_t> out(wtris.size());
        float err = 0.0f;
        const size_t n = meshopt_simplify(out.data(), wtris.data(), wtris.size(), &wpos[0].x, wpos.size(), sizeof(vec3), std::max<size_t>(target / 3 * 3, 3), FLT_MAX, 0, &err);
        if (n >= 3) {
            out.resize(n);
            sim = std::move(out);
        }
        target = (size_t)((float)target * CLOTH_SIMPLIFY_SHRINK);
    }
    const uint32_t particle_count = unique_count(sim);
    if (particle_count > want || particle_count < 3) {
        log_write("MeshCooker: cloth on '%s' could not be reduced to %u particles, skipped.", root_name.c_str(), want);
        return false;
    }

    std::unordered_map<uint32_t, uint16_t> local_of;
    std::vector<uint32_t> particle_weld;
    for (uint32_t w : sim) {
        if (local_of.try_emplace(w, (uint16_t)particle_weld.size()).second) particle_weld.push_back(w);
    }

    LMeshCloth cloth{};
    cloth.joint_first = (uint32_t)r_joints.size();
    cloth.particle_first = (uint32_t)r_out.particles.size();
    cloth.particle_count = (uint32_t)particle_weld.size();
    cloth.root_joint = LMESH_CLOTH_NO_ROOT;
    cloth.slot = p_region.slot;

    std::vector<float> free_share(particle_weld.size());
    for (size_t i = 0; i < particle_weld.size(); i++) free_share[i] = std::clamp(p_region.free[wsrc[particle_weld[i]]], 0.0f, 1.0f);

    for (size_t i = 0; i < particle_weld.size(); i++) {
        const uint32_t src = wsrc[particle_weld[i]];
        LMeshClothParticle p{};
        p.rest = wpos[particle_weld[i]];
        p.max_distance = free_share[i] < CLOTH_PINNED ? 0.0f : free_share[i];
        p.joints = p_original[src].joints;
        p.weights = p_original[src].weights;
        r_out.particles.push_back(p);
        r_joints.push_back("__cloth" + std::to_string(p_index) + "_" + std::to_string(i));
        r_inverse_binds.push_back(translate(mat4(1.0f), -p.rest));
    }

    std::unordered_map<uint32_t, std::vector<uint16_t>> edges;
    cloth.triangle_first = (uint32_t)r_out.triangles.size();
    for (size_t t = 0; t + 2 < sim.size(); t += 3) {
        const uint16_t a = local_of[sim[t]], b = local_of[sim[t + 1]], c = local_of[sim[t + 2]];
        r_out.triangles.push_back(u16vec4(a, b, c, 0u));
        const uint16_t tri[3] = { a, b, c };
        for (int e = 0; e < 3; e++) {
            const uint16_t u = tri[e], v = tri[(e + 1) % 3], o = tri[(e + 2) % 3];
            const uint32_t key = ((uint32_t)std::min(u, v) << 16) | std::max(u, v);
            edges[key].push_back(o);
        }
    }
    cloth.triangle_count = (uint32_t)r_out.triangles.size() - cloth.triangle_first;

    const LMeshClothParticle* parts = r_out.particles.data() + cloth.particle_first;
    cloth.link_first = (uint32_t)r_out.links.size();
    for (const auto& [key, opposite] : edges) {
        const uint16_t a = (uint16_t)(key >> 16), b = (uint16_t)(key & 0xFFFFu);
        r_out.links.push_back({ a, b, length(parts[a].rest - parts[b].rest) });
    }
    cloth.stretch_count = (uint32_t)r_out.links.size() - cloth.link_first;
    for (const auto& [key, opposite] : edges) {
        if (opposite.size() < 2 || opposite[0] == opposite[1]) continue;
        const uint16_t a = opposite[0], b = opposite[1];
        const uint32_t pair = ((uint32_t)std::min(a, b) << 16) | std::max(a, b);
        if (edges.contains(pair)) continue;
        r_out.links.push_back({ a, b, length(parts[a].rest - parts[b].rest) });
    }
    cloth.bend_count = (uint32_t)r_out.links.size() - cloth.link_first - cloth.stretch_count;

    cloth.restore_first = (uint32_t)r_out.restore.size();
    size_t bound = 0;
    const u16vec4* tris = r_out.triangles.data() + cloth.triangle_first;
    for (size_t v = 0; v < vertex_count; v++) {
        if (!p_region.vertex[v]) continue;
        float best = FLT_MAX;
        uint32_t best_tri = 0;
        vec3 best_bary(1.0f, 0.0f, 0.0f);
        for (uint32_t t = 0; t < cloth.triangle_count; t++) {
            vec3 bary;
            const vec3 q = _closest_on_triangle(p_positions[v], parts[tris[t].x].rest, parts[tris[t].y].rest, parts[tris[t].z].rest, bary);
            const vec3 d = q - p_positions[v];
            const float d2 = dot(d, d);
            if (d2 < best) {
                best = d2;
                best_tri = t;
                best_bary = bary;
            }
        }
        const u16vec4 tri = tris[best_tri];
        SkinVertex sv{};
        sv.joints = u8vec4((uint8_t)(cloth.joint_first + tri.x), (uint8_t)(cloth.joint_first + tri.y), (uint8_t)(cloth.joint_first + tri.z), (uint8_t)(cloth.joint_first + tri.x));
        sv.weights = _quantize_weights(best_bary);
        r_out.restore.push_back({ (uint32_t)v, p_original[v] });
        r_skin[v] = sv;
        bound++;
    }
    cloth.restore_count = (uint32_t)r_out.restore.size() - cloth.restore_first;

    r_out.cloths.push_back(cloth);
    log_write("MeshCooker: cloth on '%s': %u particles, %u stretch and %u bend links, %zu vertices bound.", root_name.c_str(), cloth.particle_count, cloth.stretch_count, cloth.bend_count, bound);
    return true;
}

bool dynamics_strip_cloths(LMeshData& r_data)
{
    LMeshDynamics& dyn = r_data.rig.dynamics;
    if (dyn.cloths.empty()) return true;
    for (const LMeshCloth& c : dyn.cloths) {
        if (c.particle_count && !c.restore_count) return false;
    }
    for (const LMeshClothRestore& r : dyn.restore) {
        if (r.vertex < r_data.skin.size()) r_data.skin[r.vertex] = r.skin;
    }
    uint32_t base = (uint32_t)r_data.rig.joints.size();
    for (const LMeshCloth& c : dyn.cloths) base = std::min(base, c.joint_first);
    r_data.rig.joints.resize(base);
    r_data.rig.inverse_binds.resize(std::min<size_t>(base, r_data.rig.inverse_binds.size()));
    dyn.cloths.clear();
    dyn.particles.clear();
    dyn.links.clear();
    dyn.triangles.clear();
    dyn.restore.clear();
    return true;
}

static void _raise_cloth_lod_error(LMeshData& r_data, const std::vector<vec3>& p_positions)
{
    std::vector<uint8_t> cloth(p_positions.size(), 0);
    for (const LMeshClothRestore& r : r_data.rig.dynamics.restore) {
        if (r.vertex < cloth.size()) cloth[r.vertex] = 1;
    }

    std::vector<float> edge_sum(r_data.groups.size(), 0.0f);
    std::vector<uint32_t> edge_count(r_data.groups.size(), 0u);
    for (const LMeshPage& page : r_data.pages) {
        for (uint32_t ci = page.cluster_first; ci < page.cluster_first + page.cluster_count && ci < r_data.clusters.size(); ci++) {
            const LMeshCluster& c = r_data.clusters[ci];
            if (c.self_group >= r_data.groups.size()) continue;
            const uint32_t base = page.vertex_first + c.vertex_offset;
            for (uint32_t t = 0; t < c.tri_count; t++) {
                const size_t at = (size_t)page.tri_first + c.tri_offset + t;
                if (at >= r_data.local_tris.size()) break;
                const uint32_t lt = r_data.local_tris[at];
                const uint32_t v[3] = { base + (lt & 0xFFu), base + ((lt >> 8) & 0xFFu), base + ((lt >> 16) & 0xFFu) };
                if (v[0] >= cloth.size() || v[1] >= cloth.size() || v[2] >= cloth.size()) continue;
                if (!cloth[v[0]] && !cloth[v[1]] && !cloth[v[2]]) continue;
                for (int k = 0; k < 3; k++) edge_sum[c.self_group] += distance(p_positions[v[k]], p_positions[v[(k + 1) % 3]]);
                edge_count[c.self_group] += 3;
            }
        }
    }
    for (size_t g = 0; g < r_data.groups.size(); g++) {
        if (edge_count[g]) r_data.groups[g].error = std::max(r_data.groups[g].error, edge_sum[g] / (float)edge_count[g] * CLOTH_LOD_EDGE_ERROR);
    }

    for (bool changed = true; changed;) {
        changed = false;
        for (const LMeshCluster& c : r_data.clusters) {
            if (c.self_group >= r_data.groups.size() || c.parent_group >= r_data.groups.size()) continue;
            if (r_data.groups[c.parent_group].error < r_data.groups[c.self_group].error) {
                r_data.groups[c.parent_group].error = r_data.groups[c.self_group].error;
                changed = true;
            }
        }
    }

    for (size_t i = r_data.nodes.size(); i-- > 0;) {
        LMeshNode& n = r_data.nodes[i];
        if (n.page == LMESH_NODE_INNER) {
            float err = 0.0f;
            for (uint32_t k = n.first; k < n.first + n.count && k < r_data.nodes.size(); k++) err = std::max(err, r_data.nodes[k].max_parent_error);
            n.max_parent_error = err;
        } else if (n.count && n.first < r_data.clusters.size()) {
            const uint32_t parent = r_data.clusters[n.first].parent_group;
            n.max_parent_error = parent < r_data.groups.size() ? r_data.groups[parent].error : FLT_MAX;
        }
    }
}

uint32_t dynamics_bake_cloths(LMeshData& r_data, const LSkeleton& p_skeleton)
{
    if (!r_data.skinned() || p_skeleton.cloths.empty() || r_data.skin.size() != r_data.vertices.size()) return 0;
    const LMeshPayloadHeader& h = r_data.header;
    std::vector<vec3> positions(r_data.vertices.size());
    for (size_t v = 0; v < positions.size(); v++) positions[v] = h.pos_min + vec3(r_data.vertices[v].position.position) / 65535.0f * h.pos_extent;

    std::vector<uint32_t> indices;
    std::vector<uint32_t> tri_slots;
    _each_triangle(r_data, true, [&](uint32_t p_a, uint32_t p_b, uint32_t p_c, uint32_t p_slot) {
        indices.insert(indices.end(), { p_a, p_b, p_c });
        tri_slots.push_back(p_slot);
    });

    LMeshRig& rig = r_data.rig;
    const std::vector<SkinVertex> original = r_data.skin;
    uint32_t baked = 0;
    for (size_t i = 0; i < p_skeleton.cloths.size(); i++) {
        const SkeletonCloth& def = p_skeleton.cloths[i];
        ClothRegion region;
        if (!_slot_region(def, r_data, tri_slots, region)) continue;
        if (_bake_cloth((uint32_t)i, def, region, positions, indices, original, r_data.skin, rig.joints, rig.inverse_binds, rig.dynamics)) baked++;
    }
    if (baked) _raise_cloth_lod_error(r_data, positions);
    return baked;
}

Error dynamics_rebake_cloths(const std::filesystem::path& p_bin, const LSkeleton& p_skeleton)
{
    using enum Error;
    LMeshData data;
    std::string why;
    if (!lmesh_load(p_bin, data, why)) {
        log_write("Cloth: couldn't load '%s' (%s).", p_bin.string().c_str(), why.c_str());
        return FAILED;
    }
    Guid guid;
    if (!data.skinned() || !lmesh_read_section(p_bin, LMeshSectionType::SKELETON, data.skeleton, &guid)) {
        log_write("Cloth: '%s' has no skeleton. Reimport it once.", p_bin.string().c_str());
        return FAILED;
    }
    lmesh_read_section(p_bin, LMeshSectionType::COLLISION, data.collision);
    if (!dynamics_strip_cloths(data)) {
        log_write("Cloth: '%s' was baked before cloth could be rebuilt in place. Reimport it once.", p_bin.string().c_str());
        return FAILED;
    }
    dynamics_bake_cloths(data, p_skeleton);
    if (!lmesh_write(p_bin, guid, data)) {
        log_write("Cloth: couldn't write '%s'.", p_bin.string().c_str());
        return FAILED;
    }
    return OK;
}

}
