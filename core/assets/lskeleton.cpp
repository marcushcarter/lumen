#include <core/assets/lskeleton.h>
#include <core/assets/asset_common.h>
#include <core/base/hash.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

namespace lumen {

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

Error LSkeleton::load_cooked(const std::filesystem::path& p_path)
{
    using enum Error;
    LAssetHeader header{};
    std::vector<uint8_t> payload;
    if (!read_cooked_asset(p_path, AssetType::SKELETON, header, payload) || payload.size() < sizeof(LSkeletonPayloadHeader)) return FAILED;

    LSkeletonPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    const size_t parent_bytes = (size_t)ph.joint_count * sizeof(int32_t);
    const size_t rest_bytes = (size_t)ph.joint_count * sizeof(JointTransform);
    if (ph.joint_count > SKELETON_MAX_JOINTS || payload.size() != sizeof(ph) + parent_bytes + rest_bytes + ph.name_bytes) return FAILED;

    const uint8_t* p = payload.data() + sizeof(ph);
    *this = LSkeleton{};
    guid = header.guid;
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
    return OK;
}

Error LSkeleton::save_cooked(const std::filesystem::path& p_path) const
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
    std::vector<uint8_t> payload(sizeof(ph) + parent_bytes + rest_bytes + text.size());
    uint8_t* p = payload.data();
    std::memcpy(p, &ph, sizeof(ph));
    if (count) {
        std::memcpy(p + sizeof(ph), parents.data(), parent_bytes);
        std::memcpy(p + sizeof(ph) + parent_bytes, rest.data(), rest_bytes);
    }
    if (!text.empty()) std::memcpy(p + sizeof(ph) + parent_bytes + rest_bytes, text.data(), text.size());
    return write_cooked_asset(p_path, AssetType::SKELETON, guid, payload.data(), payload.size()) ? OK : FAILED;
}

}
