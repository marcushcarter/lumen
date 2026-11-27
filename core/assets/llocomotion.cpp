#include <core/assets/llocomotion.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>
#include <string>

namespace lumen {

struct LocoSlotInfo
{
    const char* label;
    const char* key;
};

static constexpr LocoSlotInfo LOCO_SLOTS[LOCO_SLOT_COUNT] = {
    { "Idle", "idle" },
    { "Walk Forward", "walk_f" },
    { "Walk Back", "walk_b" },
    { "Walk Left (Hips Forward)", "walk_lf" },
    { "Walk Left (Hips Back)", "walk_lb" },
    { "Walk Right (Hips Forward)", "walk_rf" },
    { "Walk Right (Hips Back)", "walk_rb" },
    { "Run Forward", "run_f" },
    { "Run Back", "run_b" },
    { "Run Left (Hips Forward)", "run_lf" },
    { "Run Left (Hips Back)", "run_lb" },
    { "Run Right (Hips Forward)", "run_rf" },
    { "Run Right (Hips Back)", "run_rb" },
    { "Sprint", "sprint_f" },
    { "Sprint Impulse", "sprint_impulse" },
    { "Turn Left 90", "turn_l90" },
    { "Turn Right 90", "turn_r90" },
    { "Turn Left 180", "turn_l180" },
    { "Turn Right 180", "turn_r180" },
    { "Rotate Left", "rotate_l" },
    { "Rotate Right", "rotate_r" },
    { "Crouch Idle", "crouch_idle" },
    { "Stand To Crouch", "crouch_enter" },
    { "Crouch To Stand", "crouch_exit" },
    { "Crouch Walk Forward", "crouch_walk_f" },
    { "Crouch Walk Back", "crouch_walk_b" },
    { "Crouch Walk Left", "crouch_walk_l" },
    { "Crouch Walk Right", "crouch_walk_r" },
    { "Crouch Turn Left 90", "crouch_turn_l90" },
    { "Crouch Turn Right 90", "crouch_turn_r90" },
    { "Crouch Turn Left 180", "crouch_turn_l180" },
    { "Crouch Turn Right 180", "crouch_turn_r180" },
    { "Crouch Rotate Left", "crouch_rotate_l" },
    { "Crouch Rotate Right", "crouch_rotate_r" },
    { "Roll", "roll" },
    { "Jump Walk (Left Foot)", "jump_walk_lf" },
    { "Jump Walk (Right Foot)", "jump_walk_rf" },
    { "Jump Run (Left Foot)", "jump_run_lf" },
    { "Jump Run (Right Foot)", "jump_run_rf" },
    { "Jump Loop", "jump_loop" },
    { "Fall Loop", "fall_loop" },
    { "Fall Loop Fast", "fall_fast" },
    { "Land Light", "land_light" },
    { "Land Heavy", "land_heavy" },
    { "Mantle 1m (Left Hand)", "mantle_1m_lh" },
    { "Mantle 1m (Right Hand)", "mantle_1m_rh" },
    { "Mantle 2m", "mantle_2m" },
};

const char* loco_slot_label(LocoSlot p_slot)
{
    return (uint32_t)p_slot < LOCO_SLOT_COUNT ? LOCO_SLOTS[(uint32_t)p_slot].label : "";
}

const char* loco_slot_key(LocoSlot p_slot)
{
    return (uint32_t)p_slot < LOCO_SLOT_COUNT ? LOCO_SLOTS[(uint32_t)p_slot].key : "";
}

LocoSlot loco_crouch_slot(LocoSlot p_slot)
{
    if (p_slot == LocoSlot::IDLE) return LocoSlot::CROUCH_IDLE;
    if (p_slot >= LocoSlot::TURN_L90 && p_slot <= LocoSlot::ROTATE_R) return (LocoSlot)((uint32_t)LocoSlot::CROUCH_TURN_L90 + ((uint32_t)p_slot - (uint32_t)LocoSlot::TURN_L90));
    return p_slot;
}

LocoSlot loco_stand_slot(LocoSlot p_slot)
{
    if (p_slot == LocoSlot::CROUCH_IDLE) return LocoSlot::IDLE;
    if (p_slot >= LocoSlot::CROUCH_TURN_L90 && p_slot <= LocoSlot::CROUCH_ROTATE_R) return (LocoSlot)((uint32_t)LocoSlot::TURN_L90 + ((uint32_t)p_slot - (uint32_t)LocoSlot::CROUCH_TURN_L90));
    return p_slot;
}

Error LLocomotionSet::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::LOCOMOTION) return FAILED;

    const LLocomotionSet def{};
    *this = def;
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    auto section = tbl[asset_type_section(AssetType::LOCOMOTION)];
    auto clips_table = section["clips"];
    for (uint32_t i = 0; i < LOCO_SLOT_COUNT; ++i) clips[i] = Guid::from_string(clips_table[LOCO_SLOTS[i].key].value_or(std::string{}));
    blend_time = (float)section["blend_time"].value_or((double)def.blend_time);
    turn_threshold = (float)section["turn_threshold"].value_or((double)def.turn_threshold);
    lean_angle = (float)section["lean_angle"].value_or((double)def.lean_angle);
    lean_acceleration = (float)section["lean_acceleration"].value_or((double)def.lean_acceleration);
    look = section["look"].value_or(def.look);
    return OK;
}

Error LLocomotionSet::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table clip_table;
    for (uint32_t i = 0; i < LOCO_SLOT_COUNT; ++i) {
        if (clips[i].valid()) clip_table.insert(LOCO_SLOTS[i].key, clips[i].to_string());
    }
    toml::table tbl {
        { "asset", toml::table{
            { "version", (int64_t)LASSET_VERSION },
            { "guid", guid.to_string() },
            { "type", (int64_t)AssetType::LOCOMOTION } }},
        { std::string(asset_type_section(AssetType::LOCOMOTION)), toml::table{
            { "blend_time", (double)blend_time },
            { "turn_threshold", (double)turn_threshold },
            { "lean_angle", (double)lean_angle },
            { "lean_acceleration", (double)lean_acceleration },
            { "look", look },
            { "clips", std::move(clip_table) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
