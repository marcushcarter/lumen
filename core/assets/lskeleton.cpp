#include <core/assets/lskeleton.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/base/hash.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

namespace lumen {

struct SkeletonClothV3
{
    int32_t root;
    uint32_t slot;
    uint32_t resolution;
    float stretch;
    float bend;
    float damping;
    float inertia;
    float thickness;
    float max_distance;
};

static_assert(sizeof(SkeletonClothV3) == 36, "SkeletonClothV3 layout changed");
static constexpr size_t SKELETON_CLOTH_V2_BYTES = 32;

mat4 JointTransform::matrix() const
{
    return translate(mat4(1.0f), translation) * mat4_cast(rotation) * glm::scale(mat4(1.0f), scale);
}

int32_t LSkeleton::find(std::string_view p_name) const
{
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == p_name) return (int32_t)i;
    }
    return SKELETON_NO_JOINT;
}

uint64_t LSkeleton::fingerprint() const
{
    uint64_t h = HASH_FNV_OFFSET;
    for (size_t i = 0; i < names.size(); ++i) {
        h = hash_fnv(names[i], h);
        h = hash_fnv_step(h, (uint64_t)(int64_t)parents[i]);
    }
    return h;
}

void LSkeleton::globals(const JointTransform* p_local, mat4* r_global) const
{
    for (size_t i = 0; i < names.size(); ++i) {
        const mat4 local = p_local[i].matrix();
        r_global[i] = parents[i] == SKELETON_NO_JOINT ? local : r_global[parents[i]] * local;
    }
}

void LSkeleton::branch(int32_t p_root, std::vector<uint8_t>& r_in_branch) const
{
    r_in_branch.assign(names.size(), 0u);
    if (p_root < 0 || p_root >= (int32_t)names.size()) return;
    r_in_branch[p_root] = 1u;
    for (size_t i = (size_t)p_root + 1; i < names.size(); ++i) {
        if (parents[i] >= 0 && r_in_branch[parents[i]]) r_in_branch[i] = 1u;
    }
}

Error LSkeleton::load_cooked(const std::filesystem::path& p_path)
{
    std::vector<uint8_t> payload;
    Guid owner;
    if (!lmesh_read_section(p_path, LMeshSectionType::SKELETON, payload, &owner) || decode(payload) != Error::OK) return Error::FAILED;
    guid = owner;
    return Error::OK;
}

Error LSkeleton::save_cooked(const std::filesystem::path& p_path) const
{
    std::vector<uint8_t> payload;
    if (encode(payload) != Error::OK) return Error::FAILED;
    return lmesh_replace_section(p_path, LMeshSectionType::SKELETON, payload) ? Error::OK : Error::FAILED;
}

Error LSkeleton::decode(const std::vector<uint8_t>& payload)
{
    using enum Error;
    if (payload.size() < sizeof(LSkeletonPayloadHeader)) return FAILED;

    LSkeletonPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    const size_t parent_bytes = (size_t)ph.joint_count * sizeof(int32_t);
    const size_t rest_bytes = (size_t)ph.joint_count * sizeof(JointTransform);
    const size_t base_bytes = sizeof(ph) + parent_bytes + rest_bytes + ph.name_bytes;
    if (ph.joint_count > SKELETON_MAX_JOINTS || payload.size() < base_bytes) return FAILED;

    const uint8_t* p = payload.data() + sizeof(ph);
    *this = LSkeleton{};
    parents.resize(ph.joint_count);
    rest.resize(ph.joint_count);
    if (ph.joint_count) {
        std::memcpy(parents.data(), p, parent_bytes);
        std::memcpy(rest.data(), p + parent_bytes, rest_bytes);
    }

    const char* text = reinterpret_cast<const char*>(p + parent_bytes + rest_bytes);
    size_t at = 0;
    names.resize(ph.joint_count);
    for (uint32_t i = 0; i < ph.joint_count; ++i) {
        const size_t len = strnlen(text + at, ph.name_bytes - at);
        if (at + len >= ph.name_bytes) return FAILED;
        names[i].assign(text + at, len);
        at += len + 1;
        if (parents[i] >= (int32_t)i) return FAILED;
    }

    if (payload.size() >= base_bytes + sizeof(LSkeletonDynamicsHeader)) {
        LSkeletonDynamicsHeader dh;
        std::memcpy(&dh, payload.data() + base_bytes, sizeof(dh));
        const bool v2 = dh.magic == SKELETON_DYNAMICS_MAGIC_V2;
        const bool v3 = dh.magic == SKELETON_DYNAMICS_MAGIC_V3;
        if (dh.magic != SKELETON_DYNAMICS_MAGIC && !v2 && !v3) {
            log_write("Skeleton: chains and cloth were saved in an older format and were dropped. Reimport the mesh to bring them back.");
            return OK;
        }
        const size_t cloth_stride = v2 ? SKELETON_CLOTH_V2_BYTES : v3 ? sizeof(SkeletonClothV3) : sizeof(SkeletonCloth);
        const size_t chain_bytes = (size_t)dh.chain_count * sizeof(SkeletonChain);
        const size_t cloth_bytes = (size_t)dh.cloth_count * cloth_stride;
        if (payload.size() != base_bytes + sizeof(dh) + chain_bytes + cloth_bytes) return FAILED;
        const uint8_t* d = payload.data() + base_bytes + sizeof(dh);
        chains.resize(dh.chain_count);
        if (chain_bytes) std::memcpy(chains.data(), d, chain_bytes);
        uint32_t dropped = 0;
        cloths.clear();
        if (v2) {
            dropped = dh.cloth_count;
        } else if (v3) {
            for (uint32_t i = 0; i < dh.cloth_count; i++) {
                SkeletonClothV3 old;
                std::memcpy(&old, d + chain_bytes + i * sizeof(old), sizeof(old));
                if (old.slot == SKELETON_CLOTH_NO_SLOT) {
                    dropped++;
                    continue;
                }
                SkeletonCloth c;
                c.slot = old.slot;
                c.resolution = old.resolution;
                c.stretch = old.stretch;
                c.bend = old.bend;
                c.damping = old.damping;
                c.inertia = old.inertia;
                c.thickness = old.thickness;
                c.max_distance = old.max_distance;
                cloths.push_back(c);
            }
        } else {
            cloths.resize(dh.cloth_count);
            if (cloth_bytes) std::memcpy(cloths.data(), d + chain_bytes, cloth_bytes);
        }
        if (dropped) log_write("Skeleton: %u bone cloth entries were dropped. Cloth is now set by material slot with vertex colour pins.", dropped);
        for (const SkeletonChain& c : chains) {
            if (c.root < 0 || c.root >= (int32_t)ph.joint_count) return FAILED;
        }
        for (const SkeletonCloth& c : cloths) {
            if (c.slot == SKELETON_CLOTH_NO_SLOT) return FAILED;
        }
    } else if (payload.size() != base_bytes) {
        return FAILED;
    }
    return OK;
}

Error LSkeleton::encode(std::vector<uint8_t>& r_payload) const
{
    using enum Error;
    const uint32_t count = joint_count();
    if (count > SKELETON_MAX_JOINTS || parents.size() != count || rest.size() != count) return FAILED;

    std::string text;
    for (const std::string& n : names) {
        text.append(n);
        text.push_back('\0');
    }

    const LSkeletonPayloadHeader ph{ fingerprint(), count, (uint32_t)text.size() };
    const size_t parent_bytes = (size_t)count * sizeof(int32_t);
    const size_t rest_bytes = (size_t)count * sizeof(JointTransform);
    const size_t base_bytes = sizeof(ph) + parent_bytes + rest_bytes + text.size();
    const bool dynamic = !chains.empty() || !cloths.empty();
    const size_t chain_bytes = chains.size() * sizeof(SkeletonChain);
    const size_t cloth_bytes = cloths.size() * sizeof(SkeletonCloth);
    std::vector<uint8_t>& payload = r_payload;
    payload.assign(base_bytes + (dynamic ? sizeof(LSkeletonDynamicsHeader) + chain_bytes + cloth_bytes : 0), 0);
    uint8_t* p = payload.data();
    std::memcpy(p, &ph, sizeof(ph));
    if (count) {
        std::memcpy(p + sizeof(ph), parents.data(), parent_bytes);
        std::memcpy(p + sizeof(ph) + parent_bytes, rest.data(), rest_bytes);
    }
    if (!text.empty()) std::memcpy(p + sizeof(ph) + parent_bytes + rest_bytes, text.data(), text.size());
    if (dynamic) {
        const LSkeletonDynamicsHeader dh{ SKELETON_DYNAMICS_MAGIC, (uint32_t)chains.size(), (uint32_t)cloths.size(), 0u };
        std::memcpy(p + base_bytes, &dh, sizeof(dh));
        if (chain_bytes) std::memcpy(p + base_bytes + sizeof(dh), chains.data(), chain_bytes);
        if (cloth_bytes) std::memcpy(p + base_bytes + sizeof(dh) + chain_bytes, cloths.data(), cloth_bytes);
    }
    return OK;
}

}
