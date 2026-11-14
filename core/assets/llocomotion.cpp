#include <core/assets/llocomotion.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

static const char* CYCLE_KEYS[CYCLE_DIRECTION_COUNT] = { "forward", "backward", "left_forward", "left_backward", "right_forward", "right_backward" };

const char* cycle_direction_name(CycleDirection p_direction)
{
    switch (p_direction) {
        case CycleDirection::FORWARD: return "Forward";
        case CycleDirection::BACKWARD: return "Backward";
        case CycleDirection::LEFT_FORWARD: return "Left (Hips Forward)";
        case CycleDirection::LEFT_BACKWARD: return "Left (Hips Backward)";
        case CycleDirection::RIGHT_FORWARD: return "Right (Hips Forward)";
        case CycleDirection::RIGHT_BACKWARD: return "Right (Hips Backward)";
        case CycleDirection::COUNT: break;
    }
    return "Forward";
}

static const char* TURN_KEYS[TURN_CLIP_COUNT] = { "turn_left_90", "turn_right_90", "turn_left_180", "turn_right_180" };

const char* turn_clip_name(TurnClip p_turn)
{
    switch (p_turn) {
        case TurnClip::LEFT_90: return "Turn Left 90";
        case TurnClip::RIGHT_90: return "Turn Right 90";
        case TurnClip::LEFT_180: return "Turn Left 180";
        case TurnClip::RIGHT_180: return "Turn Right 180";
        case TurnClip::COUNT: break;
    }
    return "Turn Left 90";
}

static std::string _guid_text(Guid p_guid)
{
    return !p_guid.valid() ? std::string{} : p_guid.to_string();
}

static Guid _guid(const toml::node_view<const toml::node>& p_node)
{
    return Guid::from_string(p_node.value_or(std::string{}));
}

static float _float(const toml::node_view<const toml::node>& p_node, float p_fallback)
{
    return (float)p_node.value_or((double)p_fallback);
}

static void _load_stance(const toml::node_view<const toml::node>& p_node, CharacterStance& r_stance)
{
    r_stance.walk_speed = _float(p_node["walk_speed"], r_stance.walk_speed);
    r_stance.run_speed = _float(p_node["run_speed"], r_stance.run_speed);
    r_stance.sprint_speed = _float(p_node["sprint_speed"], r_stance.sprint_speed);
    r_stance.acceleration = _float(p_node["acceleration"], r_stance.acceleration);
    r_stance.braking = _float(p_node["braking"], r_stance.braking);
    r_stance.rotation_rate = _float(p_node["rotation_rate"], r_stance.rotation_rate);
    r_stance.idle = _guid(p_node["idle"]);
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) r_stance.cycles[d] = _guid(p_node[CYCLE_KEYS[d]]);
    for (uint32_t t = 0; t < TURN_CLIP_COUNT; t++) r_stance.turns[t] = _guid(p_node[TURN_KEYS[t]]);
    r_stance.rotate_left = _guid(p_node["rotate_left"]);
    r_stance.rotate_right = _guid(p_node["rotate_right"]);
}

static toml::table _save_stance(const CharacterStance& p_stance)
{
    toml::table t{
        { "walk_speed", (double)p_stance.walk_speed },
        { "run_speed", (double)p_stance.run_speed },
        { "sprint_speed", (double)p_stance.sprint_speed },
        { "acceleration", (double)p_stance.acceleration },
        { "braking", (double)p_stance.braking },
        { "rotation_rate", (double)p_stance.rotation_rate },
        { "idle", _guid_text(p_stance.idle) },
    };
    for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) t.insert_or_assign(CYCLE_KEYS[d], _guid_text(p_stance.cycles[d]));
    for (uint32_t i = 0; i < TURN_CLIP_COUNT; i++) t.insert_or_assign(TURN_KEYS[i], _guid_text(p_stance.turns[i]));
    t.insert_or_assign("rotate_left", _guid_text(p_stance.rotate_left));
    t.insert_or_assign("rotate_right", _guid_text(p_stance.rotate_right));
    return t;
}

Error LLocomotion::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    const toml::table& root = tbl;
    auto asset = root["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::LOCOMOTION) return FAILED;

    *this = LLocomotion{};
    guid = _guid(asset["guid"]);
    auto section = root[root.contains(asset_type_section(AssetType::LOCOMOTION)) ? asset_type_section(AssetType::LOCOMOTION) : std::string_view("character")];
    _load_stance(section["standing"], standing);
    _load_stance(section["crouching"], crouching);
    jump_speed = _float(section["jump_speed"], jump_speed);
    air_control = _float(section["air_control"], air_control);
    gravity = _float(section["gravity"], gravity);
    jump_standing = _guid(section["jump_standing"]);
    if (!jump_standing.valid()) jump_standing = _guid(section["jump"]);
    jump_walk_left = _guid(section["jump_walk_left"]);
    jump_walk_right = _guid(section["jump_walk_right"]);
    jump_left = _guid(section["jump_left"]);
    jump_right = _guid(section["jump_right"]);
    jump_loop = _guid(section["jump_loop"]);
    fall_fast = _guid(section["fall_fast"]);
    fast_fall_speed = _float(section["fast_fall_speed"], fast_fall_speed);
    fall = _guid(section["fall"]);
    land_light = _guid(section["land_light"]);
    if (!land_light.valid()) land_light = _guid(section["land"]);
    land_heavy = _guid(section["land_heavy"]);
    flip_feet = section["flip_feet"].value_or(flip_feet);
    heavy_land_speed = _float(section["heavy_land_speed"], heavy_land_speed);
    lean_angle = _float(section["lean_angle"], lean_angle);
    air_lean_angle = _float(section["air_lean_angle"], air_lean_angle);
    for (int i = 0; i < 3; i++) lean_bones[i] = section["lean_bone_" + std::to_string(i + 1)].value_or(lean_bones[i]);
    look_at = section["look_at"].value_or(look_at);
    look_yaw_limit = _float(section["look_yaw_limit"], look_yaw_limit);
    look_pitch_limit = _float(section["look_pitch_limit"], look_pitch_limit);
    neck_bone = section["neck_bone"].value_or(neck_bone);
    head_bone = section["head_bone"].value_or(head_bone);
    foot_ik = section["foot_ik"].value_or(foot_ik);
    pelvis_bone = section["pelvis_bone"].value_or(pelvis_bone);
    static const char* SIDES[2] = { "_l", "_r" };
    for (int i = 0; i < 2; i++) {
        thigh_bones[i] = section[std::string("thigh_bone") + SIDES[i]].value_or(thigh_bones[i]);
        calf_bones[i] = section[std::string("calf_bone") + SIDES[i]].value_or(calf_bones[i]);
        foot_bones[i] = section[std::string("foot_bone") + SIDES[i]].value_or(foot_bones[i]);
    }
    stand_to_crouch = _guid(section["stand_to_crouch"]);
    crouch_to_stand = _guid(section["crouch_to_stand"]);
    stop_left = _guid(section["stop_left"]);
    stop_right = _guid(section["stop_right"]);
    transition_left = _guid(section["transition_left"]);
    transition_right = _guid(section["transition_right"]);
    return OK;
}

Error LLocomotion::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", _guid_text(guid) },
            { "type", static_cast<int64_t>(AssetType::LOCOMOTION) } }},
        { std::string(asset_type_section(AssetType::LOCOMOTION)), toml::table{
            { "standing", _save_stance(standing) },
            { "crouching", _save_stance(crouching) },
            { "jump_speed", (double)jump_speed },
            { "air_control", (double)air_control },
            { "gravity", (double)gravity },
            { "jump_standing", _guid_text(jump_standing) },
            { "jump_walk_left", _guid_text(jump_walk_left) },
            { "jump_walk_right", _guid_text(jump_walk_right) },
            { "jump_left", _guid_text(jump_left) },
            { "jump_right", _guid_text(jump_right) },
            { "jump_loop", _guid_text(jump_loop) },
            { "fall_fast", _guid_text(fall_fast) },
            { "fast_fall_speed", (double)fast_fall_speed },
            { "fall", _guid_text(fall) },
            { "land_light", _guid_text(land_light) },
            { "land_heavy", _guid_text(land_heavy) },
            { "flip_feet", flip_feet },
            { "heavy_land_speed", (double)heavy_land_speed },
            { "lean_angle", (double)lean_angle },
            { "air_lean_angle", (double)air_lean_angle },
            { "lean_bone_1", lean_bones[0] },
            { "lean_bone_2", lean_bones[1] },
            { "lean_bone_3", lean_bones[2] },
            { "look_at", look_at },
            { "look_yaw_limit", (double)look_yaw_limit },
            { "look_pitch_limit", (double)look_pitch_limit },
            { "neck_bone", neck_bone },
            { "head_bone", head_bone },
            { "foot_ik", foot_ik },
            { "pelvis_bone", pelvis_bone },
            { "thigh_bone_l", thigh_bones[0] },
            { "thigh_bone_r", thigh_bones[1] },
            { "calf_bone_l", calf_bones[0] },
            { "calf_bone_r", calf_bones[1] },
            { "foot_bone_l", foot_bones[0] },
            { "foot_bone_r", foot_bones[1] },
            { "stand_to_crouch", _guid_text(stand_to_crouch) },
            { "crouch_to_stand", _guid_text(crouch_to_stand) },
            { "stop_left", _guid_text(stop_left) },
            { "stop_right", _guid_text(stop_right) },
            { "transition_left", _guid_text(transition_left) },
            { "transition_right", _guid_text(transition_right) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
