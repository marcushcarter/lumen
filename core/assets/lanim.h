#pragma once
#include <core/assets/lskeleton.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace lumen {

inline constexpr uint32_t ANIM_MAX_TRACKS = SKELETON_MAX_JOINTS;
inline constexpr uint32_t ANIM_MAX_FRAMES = 1u << 20;

struct LAnimPayloadHeader
{
    Guid skeleton;
    float duration;
    float sample_rate;
    uint32_t frame_count;
    uint32_t track_count;
    uint32_t name_bytes;
    uint32_t _pad;
};

static_assert(sizeof(LAnimPayloadHeader) == 32, "LAnimPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LAnimPayloadHeader>, "must be blittable");

struct LAnimation
{
    Guid guid;
    Guid skeleton;
    float duration = 0.0f;
    float sample_rate = 30.0f;
    uint32_t frame_count = 0;
    std::vector<std::string> tracks;
    std::vector<JointTransform> samples;

    uint32_t track_count() const { return (uint32_t)tracks.size(); }
    JointTransform sample(uint32_t p_track, float p_time) const;

    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
};

}
