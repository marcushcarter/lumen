#include <editor/viewport/orbit_camera.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace lumen {

static constexpr float MAX_PITCH = 1.55f;
static constexpr float FRAME_MARGIN = 1.1f;
static constexpr float MIN_RADIUS_SCALE = 0.02f;
static constexpr float MAX_FIT_SCALE = 10.0f;

static glm::vec3 _offset(float p_yaw, float p_pitch)
{
    const float cp = std::cos(p_pitch);
    return glm::vec3(cp * std::sin(p_yaw), std::sin(p_pitch), cp * std::cos(p_yaw));
}

void OrbitCamera::rotate(float p_dx, float p_dy)
{
    yaw -= p_dx * rotate_speed;
    pitch = glm::clamp(pitch + p_dy * rotate_speed, -MAX_PITCH, MAX_PITCH);
    yaw = std::fmod(yaw, glm::two_pi<float>());
}

void OrbitCamera::zoom(float p_wheel)
{
    distance = glm::clamp(distance * std::pow(1.0f - zoom_step, p_wheel), min_distance, max_distance);
}

void OrbitCamera::pan(float p_dx, float p_dy, float p_view_height)
{
    if (p_view_height < 1.0f) return;
    const float units = 2.0f * distance * std::tan(camera.fov_y * 0.5f) / p_view_height;
    const glm::vec3 right = camera.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = camera.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    target += (-right * p_dx + up * p_dy) * units;
}

void OrbitCamera::move(const glm::vec3& p_axes, float p_dt)
{
    if (glm::dot(p_axes, p_axes) <= 0.0f || p_dt <= 0.0f) return;
    const glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(-forward.z, 0.0f, forward.x);
    const glm::vec3 dir = right * p_axes.x + glm::vec3(0.0f, 1.0f, 0.0f) * p_axes.y + forward * p_axes.z;
    target += glm::normalize(dir) * std::max(distance * MOVE_SPEED, MIN_MOVE_SPEED) * speed * p_dt;
}

void OrbitCamera::control(const Input& p_input, float p_view_height, float p_dt)
{
    if (p_input.rotate != glm::vec2(0.0f)) rotate(p_input.rotate.x, p_input.rotate.y);
    if (p_input.pan != glm::vec2(0.0f)) pan(p_input.pan.x, p_input.pan.y, p_view_height);
    if (p_input.wheel != 0.0f) zoom(p_input.wheel);
    const ImGuiIO& io = ImGui::GetIO();
    if (!p_input.keys || io.WantTextInput || io.KeyCtrl || io.KeyAlt) return;
    const auto key = [](ImGuiKey p_key) { return ImGui::IsKeyDown(p_key) ? 1.0f : 0.0f; };
    move(glm::vec3(key(ImGuiKey_D) - key(ImGuiKey_A), key(ImGuiKey_E) - key(ImGuiKey_Q), key(ImGuiKey_W) - key(ImGuiKey_S)), p_dt);
}

void OrbitCamera::frame(glm::vec3 p_center, float p_radius)
{
    const float radius = glm::max(p_radius, 0.001f);
    const float fit = radius / std::sin(camera.fov_y * 0.5f) * FRAME_MARGIN;
    min_distance = glm::max(radius * MIN_RADIUS_SCALE, camera.near_z * 2.0f);
    max_distance = glm::max(fit * MAX_FIT_SCALE, min_distance);
    if (keep_view) {
        keep_view = false;
        distance = glm::clamp(distance, min_distance, max_distance);
        return;
    }
    target = p_center;
    distance = glm::clamp(fit, min_distance, max_distance);
}

void OrbitCamera::look_from(glm::vec3 p_position, glm::vec3 p_target)
{
    const glm::vec3 offset = p_position - p_target;
    const float len = glm::length(offset);
    if (len < 1e-4f) return;
    const glm::vec3 dir = offset / len;
    target = p_target;
    distance = glm::clamp(len, min_distance, max_distance);
    pitch = glm::clamp(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)), -MAX_PITCH, MAX_PITCH);
    yaw = std::atan2(dir.x, dir.z);
    update();
}

void OrbitCamera::place(const Camera& p_camera)
{
    const glm::vec3 forward = glm::normalize(p_camera.forward());
    look_from(p_camera.position, p_camera.position + forward * distance);
}

void OrbitCamera::update()
{
    const glm::vec3 offset = _offset(yaw, pitch);
    camera.position = target + offset * distance;
    camera.rotation = glm::quatLookAt(-offset, glm::vec3(0.0f, 1.0f, 0.0f));
}

OrbitCamera::Input OrbitDrag::update(bool p_hovered, bool p_can_start)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (p_hovered && p_can_start && !panning && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rotating = true;
    if (p_hovered && p_can_start && !rotating && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) rotating = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning = false;
    OrbitCamera::Input input;
    if (rotating) input.rotate = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
    if (panning) input.pan = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
    input.wheel = p_hovered ? io.MouseWheel : 0.0f;
    input.keys = p_hovered || active();
    return input;
}

}
