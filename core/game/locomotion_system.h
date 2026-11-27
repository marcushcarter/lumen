#pragma once
#include <core/animation/clip_player.h>
#include <core/animation/two_bone_ik.h>
#include <core/assets/lanim.h>
#include <core/assets/llocomotion.h>
#include <core/game/game_session.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>

namespace lumen {

struct Renderer;
struct Project;

struct LocomotionSystem : GameSystem
{
    static constexpr double SET_POLL_S = 1.0;
    static constexpr float START_SPEED = 0.2f;
    static constexpr float IDLE_SPEED = 0.15f;
    static constexpr float MIN_RATE = 0.3f;
    static constexpr float MAX_RATE = 2.0f;
    static constexpr float LEAN_SMOOTH = 6.0f;
    static constexpr float LOOK_SMOOTH = 10.0f;
    static constexpr float LOOK_YAW_MAX = 70.0f;
    static constexpr float LOOK_PITCH_MAX = 60.0f;
    static constexpr float ROOT_MOTION_TURN = 5.0f;
    static constexpr float HIP_SWITCH = 0.25f;
    static constexpr float TURN_BIG = 112.5f;
    static constexpr float TURN_DECIDE = 0.2f;
    static constexpr float TURN_UPGRADE = 0.3f;
    static constexpr float ROTATE_DONE = 5.0f;
    static constexpr float STANCE_UPPER = 0.7f;
    static constexpr float STANCE_FASTEST = 0.35f;
    static constexpr float STRIDE_LIFT = 0.5f;
    static constexpr float IMPULSE_MIN_ACCEL = 0.5f;
    static constexpr float IMPULSE_RISE = 6.0f;
    static constexpr float IMPULSE_FALL = 1.5f;
    static constexpr float IMPULSE_ARMS = 0.6f;
    static constexpr float IMPULSE_LEAN = 0.5f;
    static constexpr uint32_t SWING_SAMPLES = 8;
    static constexpr float ROLL_RATE_MIN = 0.8f;
    static constexpr float ROLL_RATE_MAX = 1.5f;
    static constexpr float ROLL_EXIT = 0.7f;
    static constexpr float ROLL_BLEND_OUT = 0.3f;
    static constexpr float ROLL_BLEND_IN = 0.25f;
    static constexpr float ROLL_SPEED_MIN = 3.0f;
    static constexpr float AIR_DELAY = 0.15f;
    static constexpr float JUMP_STILL = 0.5f;
    static constexpr float AIR_APEX = 1.5f;
    static constexpr float FALL_FAST_FROM = 7.0f;
    static constexpr float FALL_FAST_RANGE = 4.0f;
    static constexpr float TAKEOFF_FADE = 0.25f;
    static constexpr float LAND_HEAVY_SPEED = 7.0f;
    static constexpr float LAND_ROLL_SPEED = 9.0f;
    static constexpr float LAND_HEAVY_KEEP = 0.4f;
    static constexpr float LAND_HEAVY_EXIT = 0.45f;
    static constexpr float DIP_TIME = 0.3f;
    static constexpr float LAND_STILL = 1.0f;
    static constexpr float MANTLE_SPLIT = 1.4f;
    static constexpr float HEAD_SHOW = 0.35f;
    static constexpr float MANTLE_FALLBACK_TIME = 0.6f;
    static constexpr float MANTLE_SCALE_MIN = 0.3f;
    static constexpr float MANTLE_SCALE_MAX = 3.0f;
    static constexpr float MANTLE_BLEND_OUT = 0.25f;
    static constexpr float MANTLE_EXIT = 0.6f;
    static constexpr float MANTLE_TOP_TOLERANCE = 0.05f;
    static constexpr float DIP_DEPTH = 0.12f;

    enum class State : uint8_t { IDLE, MOVE, TURN, ROTATE, TRANSITION, ROLL, AIR, LAND, MANTLE };

    struct Clip {
        LAnimation anim;
        bool loaded = false;
        float cycle_offset = 0.0f;
        std::vector<vec3> path;
        Guid path_skeleton;
    };

    struct Set {
        LLocomotionSet data;
        bool loaded = false;
        std::filesystem::file_time_type time{};
        double poll = -1.0e9;
    };

    struct Binding {
        std::vector<int32_t> map;
        ClipRetarget retarget;
        int32_t root_joint = SKELETON_NO_JOINT;
        bool root_bone = false;
    };

    struct Character {
        Guid skeleton;
        Guid set;
        State state = State::IDLE;
        LocoSlot slot = LocoSlot::IDLE;
        float time = 0.0f;
        float cycle = 0.0f;
        float turn_total = 0.0f;
        float turn_start = 0.0f;
        float turn_wait = 0.0f;
        float turn_clip = 0.0f;
        bool hips_back = false;
        bool foot_down[2] = {};
        bool foot_landed = false;
        bool foot_grip = true;
        bool aligning = false;
        bool crouched = false;
        float stance = 0.0f;
        float stance_from = 0.0f;
        float stance_to = 0.0f;
        float stance_time = 0.0f;
        float stance_x = 1.0f;
        float stance_duration = 0.0f;
        LocoSlot stance_slot = LocoSlot::COUNT;
        std::vector<JointTransform> stance_pose;
        std::vector<uint8_t> upper;
        bool blend_pending = false;
        State sampled = State::IDLE;
        float blend_elapsed = 0.0f;
        float blend_time = 0.0f;
        std::vector<quat> blend_rot;
        std::vector<vec3> blend_pos;
        std::vector<JointTransform> last;
        std::vector<JointTransform> out;
        std::vector<JointTransform> scratch;
        TwoBoneChain legs[2];
        float impulse = 0.0f;
        float roll_rate = 1.0f;
        vec3 roll_velocity = vec3(0.0f);
        float roll_entry = 0.0f;
        LocoSlot takeoff = LocoSlot::COUNT;
        float air_clock = 0.0f;
        float dip = 0.0f;
        float dip_time = 0.0f;
        vec3 mantle_last = vec3(0.0f);
        float yaw_blend = 0.0f;
        float yaw_elapsed = 0.0f;
        float yaw_time = 0.0f;
        float prev_speed = 0.0f;
        std::vector<int32_t> arms;
        std::vector<quat> swing_mean;
        std::vector<JointTransform> impulse_pose;
        int32_t pelvis = SKELETON_NO_JOINT;
        int32_t top = SKELETON_NO_JOINT;
        bool legs_ready = false;
        std::vector<int32_t> lean_chain;
        vec2 lean = vec2(0.0f);
        vec3 prev_velocity = vec3(0.0f);
        float prev_facing = 0.0f;
        bool prev_ready = false;
        float look_yaw = 0.0f;
        float look_pitch = 0.0f;
        std::vector<int32_t> look_chain;
        std::vector<float> look_weight;
        int32_t head = SKELETON_NO_JOINT;
        bool seen = false;
    };

    const Renderer* renderer = nullptr;
    const Project* project = nullptr;
    std::function<std::filesystem::path(Guid)> locate;

    std::unordered_map<Guid, Clip> clips;
    std::unordered_map<Guid, Set> sets;
    std::unordered_map<Guid, LSkeleton> authored;
    std::unordered_map<uint64_t, Binding> bindings;
    std::unordered_map<uint64_t, Character> characters;
    std::vector<mat4> _globals;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    const LLocomotionSet* _set(Guid p_guid);
    const Clip* _clip(Guid p_guid);
    const Binding* _binding(Guid p_clip, const Clip& p_data, const LSkeleton& p_skeleton, Guid p_skeleton_guid);
    bool _sample(const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_time, std::vector<JointTransform>& r_pose);
    const Clip* _slot_clip(const LLocomotionSet& p_set, LocoSlot p_slot);
    void _evaluate(Scene& r_world, Entity p_entity, Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_dt);
    bool _cycle(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, const MovementComponent* p_move, float p_dt, bool p_crouch, bool p_advance, std::vector<JointTransform>& r_pose);
    static bool _one_shot(LocoSlot p_slot);
    const std::vector<vec3>* _mantle_path(Character& r_char, const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid);
    void _dip(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, float p_amount);
    void _effort(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid);
    vec3 _travel(const LLocomotionSet& p_set, LocoSlot p_slot, const LSkeleton& p_skeleton, Guid p_skeleton_guid, float p_time);
    void _push_root(MovementComponent* r_move, const Clip& p_clip, const vec3& p_model_delta, float p_time);
    void _legs(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton);
    void _stride_warp(Character& r_char, const LSkeleton& p_skeleton, std::vector<JointTransform>& r_pose, float p_stride, const vec3& p_dir);
    void _stance_sample(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, Guid p_skeleton_guid, LocoSlot p_slot, float p_time, bool p_normalized, std::vector<JointTransform>& r_pose);
    void _lean(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, const MovementComponent* p_move, float p_dt);
    void _look(Character& r_char, const LLocomotionSet& p_set, const LSkeleton& p_skeleton, const MovementComponent* p_move, float p_dt);
    void _blend(Character& r_char, float p_dt);
    void _switch(Character& r_char, State p_state, LocoSlot p_slot, const LLocomotionSet& p_set);
    void _clear();
};

}
