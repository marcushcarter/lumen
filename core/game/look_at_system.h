#pragma once
#include <core/assets/guid.h>
#include <core/game/game_session.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <unordered_map>
#include <vector>

namespace lumen {

struct Renderer;

struct LookAtSystem : GameSystem
{
    static constexpr float MIN_SCORE = 0.05f;
    static constexpr float KEEP = 1.2f;
    static constexpr uint64_t NO_FOCUS = UINT64_MAX;

    struct Target {
        uint64_t key = NO_FOCUS;
        vec3 position = vec3(0.0f);
        float priority = 0.0f;
        float radius = 0.0f;
    };

    struct Looker {
        Guid skeleton;
        std::vector<int32_t> chain;
        uint32_t spines = 0;
        bool neck = false;
        int32_t head = SKELETON_NO_JOINT;
        float yaw = 0.0f;
        float pitch = 0.0f;
        uint64_t focus = NO_FOCUS;
        bool seen = false;
    };

    const Renderer* renderer = nullptr;

    std::unordered_map<uint64_t, Looker> lookers;
    std::vector<Target> _targets;
    std::vector<mat4> _globals;

    void start(GameSession& r_session) override;
    void update(GameSession& r_session, float p_dt) override;
    void stop(GameSession& r_session) override;

    void _gather(const Scene& p_world);
    void _rig(Looker& r_looker, const LSkeleton& p_skeleton, const CameraComponent* p_camera);
    void _look(const Scene& p_world, Entity p_entity, Looker& r_looker, const LookAtComponent& p_look, const LSkeleton& p_skeleton, PoseComponent& r_pose, float p_dt);
};

}
