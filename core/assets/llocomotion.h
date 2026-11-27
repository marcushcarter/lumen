#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

enum class LocoSlot : uint8_t
{
    IDLE,
    WALK_F,
    WALK_B,
    WALK_LF,
    WALK_LB,
    WALK_RF,
    WALK_RB,
    RUN_F,
    RUN_B,
    RUN_LF,
    RUN_LB,
    RUN_RF,
    RUN_RB,
    SPRINT_F,
    SPRINT_IMPULSE,
    TURN_L90,
    TURN_R90,
    TURN_L180,
    TURN_R180,
    ROTATE_L,
    ROTATE_R,
    CROUCH_IDLE,
    CROUCH_ENTER,
    CROUCH_EXIT,
    CROUCH_WALK_F,
    CROUCH_WALK_B,
    CROUCH_WALK_L,
    CROUCH_WALK_R,
    CROUCH_TURN_L90,
    CROUCH_TURN_R90,
    CROUCH_TURN_L180,
    CROUCH_TURN_R180,
    CROUCH_ROTATE_L,
    CROUCH_ROTATE_R,
    ROLL,
    JUMP_WALK_LF,
    JUMP_WALK_RF,
    JUMP_RUN_LF,
    JUMP_RUN_RF,
    JUMP_LOOP,
    FALL_LOOP,
    FALL_FAST,
    LAND_LIGHT,
    LAND_HEAVY,
    COUNT,
};

inline constexpr uint32_t LOCO_SLOT_COUNT = (uint32_t)LocoSlot::COUNT;

const char* loco_slot_label(LocoSlot p_slot);
const char* loco_slot_key(LocoSlot p_slot);
LocoSlot loco_crouch_slot(LocoSlot p_slot);
LocoSlot loco_stand_slot(LocoSlot p_slot);

struct LLocomotionSet
{
    static constexpr const char* EXTENSION = ".llocomotion";

    Guid guid;
    Guid clips[LOCO_SLOT_COUNT] = {};
    float blend_time = 0.2f;
    float turn_threshold = 70.0f;
    float lean_angle = 10.0f;
    float lean_acceleration = 4.0f;
    bool look = true;

    Guid clip(LocoSlot p_slot) const { return clips[(uint32_t)p_slot]; }

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LLocomotionSet&) const = default;
};

}
