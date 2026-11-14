#include <core/game/game_input.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

namespace lumen {

static float _analog(ImGuiKey p_key)
{
    const ImGuiKeyData* data = ImGui::GetKeyData(p_key);
    return data ? data->AnalogValue : 0.0f;
}

static vec2 _stick(ImGuiKey p_left, ImGuiKey p_right, ImGuiKey p_down, ImGuiKey p_up)
{
    vec2 v(_analog(p_right) - _analog(p_left), _analog(p_up) - _analog(p_down));
    const float len = length(v);
    if (len < GameInput::STICK_DEADZONE) return vec2(0.0f);
    return v * (std::fmin((len - GameInput::STICK_DEADZONE) / (1.0f - GameInput::STICK_DEADZONE), 1.0f) / len);
}

void GameInput::clear()
{
    *this = GameInput{};
}

void GameInput::read(bool p_captured, vec2 p_mouse_delta, float p_dt)
{
    clear();
    captured = p_captured;
    if (!p_captured) return;

    vec2 keys(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) keys.y += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) keys.y -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) keys.x += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) keys.x -= 1.0f;
    if (length(keys) > 1.0f) keys = normalize(keys);
    const vec2 stick = _stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp);
    move = length(stick) > length(keys) ? stick : keys;

    look = p_mouse_delta * MOUSE_SENSITIVITY;
    look += _stick(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight, ImGuiKey_GamepadRStickDown, ImGuiKey_GamepadRStickUp) * vec2(1.0f, -1.0f) * STICK_LOOK_RATE * p_dt;

    sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_GamepadL3);
    aim = ImGui::IsMouseDown(ImGuiMouseButton_Right) || _analog(ImGuiKey_GamepadL2) > 0.5f;
    jump = ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
    crouch_toggle = ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    walk_toggle = ImGui::IsKeyPressed(ImGuiKey_LeftCtrl, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR3, false);
    rotation_toggle = ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false);
}

}
