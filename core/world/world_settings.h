#pragma once
#include <glm/glm.hpp>
#include <core/assets/guid.h>
#include <cstdint>

namespace lumen {

using namespace glm;

struct EnvironmentSettings
{
    static constexpr vec3 SUN_DIRECTION = vec3(-0.35f, -0.8f, -0.48f);
    static constexpr float SUN_ILLUMINANCE_LUX = 128000.0f;
    static constexpr float SUN_ANGULAR_RADIUS = 0.0046525f;

    float manual_ev100 = 15.0f;
};

enum class CameraState : uint8_t { EXPLORE, SPRINT, BLOCK, AIM, LOCK_ON, MOUNTED, COUNT };

static constexpr uint32_t CAMERA_STATE_COUNT = (uint32_t)CameraState::COUNT;

struct CameraStateSettings
{
    float distance = 3.2f;
    vec2 offset = vec2(0.4f, 0.15f);
    float fov = 60.0f;
    float enter_time = 0.35f;
    float exit_time = 0.35f;
};

struct CameraSettings
{
    float pivot_height = 1.55f;
    float pivot_lag = 0.06f;
    float pivot_lag_vertical = 0.15f;
    float pitch_min = -70.0f;
    float pitch_max = 60.0f;
    float start_pitch = -10.0f;
    float mouse_sensitivity = 0.08f;
    float stick_sensitivity = 180.0f;
    bool invert_y = false;
    float distance_scale = 1.0f;
    float probe_radius = 0.2f;
    float return_time = 0.4f;
    CameraStateSettings states[CAMERA_STATE_COUNT] = {
        { 3.2f, vec2(0.4f, 0.15f), 60.0f, 0.35f, 0.35f },
        { 3.6f, vec2(0.4f, 0.15f), 65.0f, 0.6f, 0.8f },
        { 2.85f, vec2(0.4f, 0.15f), 58.0f, 0.25f, 0.35f },
        { 1.4f, vec2(0.65f, 0.2f), 52.0f, 0.12f, 0.3f },
        { 3.7f, vec2(0.25f, 0.25f), 63.0f, 0.4f, 0.5f },
        { 5.0f, vec2(0.3f, 0.6f), 65.0f, 0.5f, 0.5f },
    };
};

struct PlayerSettings
{
    Guid controlled;
    CameraSettings camera;
};

}
