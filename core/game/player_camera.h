#pragma once
#include <core/world/camera.h>
#include <core/world/world.h>
#include <core/world/world_settings.h>
#include <cstdint>

namespace lumen {

struct Input;
struct PhysicsWorld;

struct PlayerCamera
{
    static constexpr float SNAP_DISTANCE = 10.0f;
    static constexpr float BLEND_TIME_SCALE = 1.0f / 3.0f;
    static constexpr float MIN_COLLISION_DISTANCE = 0.05f;

    bool initialized = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    vec3 pivot = vec3(0.0f);
    CameraState state = CameraState::EXPLORE;
    float blend_time = 0.35f;
    float distance = 3.2f;
    vec2 offset = vec2(0.4f, 0.15f);
    float fov = 60.0f;
    float collision_distance = 3.2f;
    uint32_t requests = 0;

    void reset() { initialized = false; requests = 0; }
    void request(CameraState p_state) { requests |= 1u << (uint32_t)p_state; }
    void update(const World& p_world, Entity p_target, const CameraSettings& p_settings, const Input* p_input, const PhysicsWorld* p_physics, float p_dt, Camera& r_camera);
};

}
