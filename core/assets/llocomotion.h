#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace lumen {

enum class CycleDirection : uint8_t { FORWARD, BACKWARD, LEFT_FORWARD, LEFT_BACKWARD, RIGHT_FORWARD, RIGHT_BACKWARD, COUNT };

inline constexpr uint32_t CYCLE_DIRECTION_COUNT = (uint32_t)CycleDirection::COUNT;

enum class TurnClip : uint8_t { LEFT_90, RIGHT_90, LEFT_180, RIGHT_180, COUNT };

inline constexpr uint32_t TURN_CLIP_COUNT = (uint32_t)TurnClip::COUNT;

CycleDirection cycle_direction_opposite(CycleDirection p_direction);
TurnClip turn_clip_opposite(TurnClip p_turn);

struct SideClip
{
    Guid clip;
    bool mirror = false;

    bool valid() const { return clip.valid(); }
    bool operator==(const SideClip&) const = default;
};

inline SideClip side_clip(Guid p_own, Guid p_other)
{
    if (p_own.valid()) return SideClip{ p_own, false };
    return SideClip{ p_other, p_other.valid() };
}

struct CharacterStance
{
    float walk_speed = 1.65f;
    float run_speed = 3.5f;
    float sprint_speed = 6.0f;
    float acceleration = 10.0f;
    float braking = 12.0f;
    float rotation_rate = 8.0f;
    Guid idle;
    Guid cycles[CYCLE_DIRECTION_COUNT];
    Guid turns[TURN_CLIP_COUNT];
    Guid rotate_left;
    Guid rotate_right;

    Guid turn(TurnClip p_turn) const { return turns[(uint32_t)p_turn]; }

    bool operator==(const CharacterStance&) const = default;
};

struct LLocomotion
{
    static constexpr const char* EXTENSION = ".llocomotion";

    Guid guid;
    CharacterStance standing;
    CharacterStance crouching{ 1.5f, 3.0f, 3.0f, 8.0f, 12.0f, 8.0f, {}, {}, {}, {}, {} };
    float jump_speed = 4.5f;
    float air_control = 0.2f;
    Guid jump_standing;
    Guid jump_walk_left;
    Guid jump_walk_right;
    Guid jump_left;
    Guid jump_right;
    Guid jump_loop;
    Guid fall_fast;
    float fast_fall_speed = 10.0f;
    Guid fall;
    Guid land_light;
    Guid land_heavy;
    bool flip_feet = false;
    float heavy_land_speed = 7.0f;
    float lean_angle = 8.0f;
    float air_lean_angle = 10.0f;
    std::string lean_bones[3] = { "spine_01", "spine_02", "spine_03" };
    bool look_at = true;
    float look_yaw_limit = 70.0f;
    float look_pitch_limit = 40.0f;
    std::string neck_bone = "neck_01";
    std::string head_bone = "head";
    bool foot_ik = true;
    std::string pelvis_bone = "pelvis";
    std::string thigh_bones[2] = { "thigh_l", "thigh_r" };
    std::string calf_bones[2] = { "calf_l", "calf_r" };
    std::string foot_bones[2] = { "foot_l", "foot_r" };
    Guid stand_to_crouch;
    Guid crouch_to_stand;
    Guid stop_left;
    Guid stop_right;
    Guid transition_left;
    Guid transition_right;
    Guid roll;
    float roll_land_speed = 7.0f;
    bool ragdoll = true;
    float ragdoll_mass = 70.0f;
    float muscle_strength = 1.0f;
    float balance = 8.0f;
    float head_hit_speed = 3.0f;
    float air_ragdoll_speed = 5.0f;
    float ragdoll_land_speed = 10.0f;
    std::string upperarm_bones[2] = { "upperarm_l", "upperarm_r" };
    std::string lowerarm_bones[2] = { "lowerarm_l", "lowerarm_r" };
    std::string hand_bones[2] = { "hand_l", "hand_r" };
    Guid get_up_front;
    Guid get_up_back;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LLocomotion&) const = default;
};

}
