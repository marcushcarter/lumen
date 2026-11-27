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

enum class ClipRole : uint8_t
{
    NONE,
    IDLE,
    CYCLE,
    START,
    STOP,
    PIVOT,
    TURN,
    JUMP,
    LAND,
    COUNT,
};

enum class ClipGait : uint8_t
{
    NONE,
    WALK,
    JOG,
    RUN,
    SPRINT,
    CROUCH,
    COUNT,
};

enum class ClipFootSide : uint8_t
{
    LEFT,
    RIGHT,
    COUNT,
};

const char* clip_role_name(ClipRole p_role);
const char* clip_gait_name(ClipGait p_gait);
const char* clip_foot_name(ClipFootSide p_side);

struct ClipContact
{
    uint32_t start = 0;
    uint32_t end = 0;
};

struct ClipFoot
{
    std::string joint;
    std::string toe;
    std::vector<ClipContact> contacts;

    bool planted(uint32_t p_frame) const;
};

struct ClipEvent
{
    std::string name;
    float time = 0.0f;

    bool operator==(const ClipEvent&) const = default;
};

struct ClipCurve
{
    std::string name;
    std::vector<vec2> keys;

    float evaluate(float p_time) const;
    bool operator==(const ClipCurve&) const = default;
};

struct ClipMeta
{
    ClipRole role = ClipRole::NONE;
    ClipGait gait = ClipGait::NONE;
    bool has_direction = false;
    float direction = 0.0f;

    std::string root_joint;
    float distance = 0.0f;
    float speed = 0.0f;
    float turn = 0.0f;
    float move_direction = 0.0f;
    vec3 displacement = vec3(0.0f);

    ClipFoot feet[(int)ClipFootSide::COUNT];
    std::vector<float> phase;
    std::vector<float> traveled;
    std::vector<vec4> trajectory;

    bool has_phase() const { return !phase.empty(); }
    bool has_root_motion() const { return distance > 0.05f; }
};

struct LAnimation
{
    Guid guid;
    Guid skeleton;
    float duration = 0.0f;
    float sample_rate = 30.0f;
    uint32_t frame_count = 0;
    std::vector<std::string> tracks;
    std::vector<JointTransform> samples;
    ClipMeta meta;
    std::vector<ClipEvent> events;
    std::vector<ClipCurve> curves;

    uint32_t track_count() const { return (uint32_t)tracks.size(); }
    uint32_t frame_at(float p_time) const;
    JointTransform sample(uint32_t p_track, float p_time) const;
    float phase_at(float p_time) const;
    float traveled_at(float p_time) const;
    vec4 trajectory_at(float p_time) const;

    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
};

}
