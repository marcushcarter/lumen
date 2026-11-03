#include <core/assets/lskeleton.h>
#include <core/assets/asset_common.h>
#include <cstring>

namespace lumen {

int32_t LSkeleton::find(std::string_view p_name) const
{
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == p_name) return (int32_t)i;
    }
    return SKELETON_NO_JOINT;
}

Error LSkeleton::load_cooked(const std::filesystem::path& p_path)
{
    using enum Error;
    LAssetHeader header{};
    std::vector<uint8_t> payload;
    if (!read_cooked_asset(p_path, AssetType::SKELETON, header, payload) || payload.size() < sizeof(LSkeletonPayloadHeader)) return FAILED;

    LSkeletonPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    const size_t joint_bytes = (size_t)ph.joint_count * sizeof(LSkeletonJoint);
    if (ph.joint_count > SKELETON_MAX_JOINTS || payload.size() != sizeof(ph) + joint_bytes + ph.name_bytes) return FAILED;

    const uint8_t* src = payload.data() + sizeof(ph);
    const char* names = reinterpret_cast<const char*>(src + joint_bytes);

    *this = LSkeleton{};
    guid = header.guid;
    joints.resize(ph.joint_count);
    for (uint32_t i = 0; i < ph.joint_count; ++i) {
        LSkeletonJoint j;
        std::memcpy(&j, src + (size_t)i * sizeof(LSkeletonJoint), sizeof(j));
        if (j.name_offset >= ph.name_bytes || j.parent >= (int32_t)i) return FAILED;
        Joint& out = joints[i];
        out.name.assign(names + j.name_offset, strnlen(names + j.name_offset, ph.name_bytes - j.name_offset));
        out.parent = j.parent;
        out.translation = j.translation;
        out.rotation = j.rotation;
        out.scale = j.scale;
        out.inverse_bind = j.inverse_bind;
    }
    return OK;
}

Error LSkeleton::save_cooked(const std::filesystem::path& p_path) const
{
    using enum Error;
    if (joints.size() > SKELETON_MAX_JOINTS) return FAILED;

    std::string names;
    std::vector<LSkeletonJoint> packed(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        const Joint& j = joints[i];
        packed[i] = { j.inverse_bind, j.rotation, j.translation, j.parent, j.scale, (uint32_t)names.size() };
        names.append(j.name);
        names.push_back('\0');
    }

    const LSkeletonPayloadHeader ph{ (uint32_t)joints.size(), (uint32_t)names.size() };
    const size_t joint_bytes = packed.size() * sizeof(LSkeletonJoint);
    std::vector<uint8_t> payload(sizeof(ph) + joint_bytes + names.size());
    std::memcpy(payload.data(), &ph, sizeof(ph));
    if (joint_bytes) std::memcpy(payload.data() + sizeof(ph), packed.data(), joint_bytes);
    if (!names.empty()) std::memcpy(payload.data() + sizeof(ph) + joint_bytes, names.data(), names.size());
    return write_cooked_asset(p_path, AssetType::SKELETON, guid, payload.data(), payload.size()) ? OK : FAILED;
}

}
