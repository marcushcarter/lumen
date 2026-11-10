#pragma once
#include <glm/glm.hpp>
#include <windows.h>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

enum class InputDevice : uint8_t { KEYBOARD_MOUSE, GAMEPAD };

enum class InputAction : uint8_t { MOVE, LOOK, RUN, WALK, JUMP, COUNT };

enum class InputActionType : uint8_t { BUTTON, AXIS };

enum class InputTrigger : uint8_t { HOLD, TOGGLE, TAP, DOUBLE_TAP };

enum class InputSource : uint8_t { KEY, KEYS_2D, MOUSE_DELTA, GAMEPAD_BUTTON, GAMEPAD_LEFT_STICK, GAMEPAD_RIGHT_STICK };

enum class GamepadButton : uint8_t { A, B, X, Y, LEFT_SHOULDER, RIGHT_SHOULDER, LEFT_TRIGGER, RIGHT_TRIGGER, VIEW, MENU, LEFT_STICK, RIGHT_STICK, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, COUNT };

struct InputBinding
{
    InputSource source = InputSource::KEY;
    InputTrigger trigger = InputTrigger::HOLD;
    uint16_t keys[4] = {};

    InputDevice device() const { return source == InputSource::GAMEPAD_BUTTON || source == InputSource::GAMEPAD_LEFT_STICK || source == InputSource::GAMEPAD_RIGHT_STICK ? InputDevice::GAMEPAD : InputDevice::KEYBOARD_MOUSE; }
};

struct InputActionState
{
    InputActionType type = InputActionType::BUTTON;
    std::vector<InputBinding> bindings;
    bool down = false;
    bool pressed = false;
    bool released = false;
    vec2 value = vec2(0.0f);
    InputDevice device = InputDevice::KEYBOARD_MOUSE;
};

struct GamepadState
{
    bool connected = false;
    bool buttons[(uint32_t)GamepadButton::COUNT] = {};
    vec2 left_stick = vec2(0.0f);
    vec2 right_stick = vec2(0.0f);
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
};

struct Input
{
    static constexpr uint32_t KEY_COUNT = 256;
    static constexpr uint16_t MOUSE_LEFT = 256;
    static constexpr uint16_t MOUSE_RIGHT = 257;
    static constexpr uint16_t MOUSE_MIDDLE = 258;
    static constexpr uint16_t MOUSE_X1 = 259;
    static constexpr uint16_t MOUSE_X2 = 260;
    static constexpr uint32_t SOURCE_COUNT = 261;
    static constexpr float TAP_TIME = 0.2f;
    static constexpr float DOUBLE_TAP_TIME = 0.3f;
    static constexpr float STICK_DEADZONE = 0.2f;
    static constexpr float DEVICE_SWITCH_THRESHOLD = 0.5f;

    struct BindingState {
        bool raw = false;
        bool latched = false;
        float held = 0.0f;
        float since_release = 1.0e9f;
        uint32_t taps = 0;
    };

    HWND hwnd = nullptr;
    bool keys[SOURCE_COUNT] = {};
    bool keys_prev[SOURCE_COUNT] = {};
    vec2 mouse_delta_pending = vec2(0.0f);
    vec2 mouse_delta = vec2(0.0f);
    GamepadState gamepad;
    GamepadState gamepad_prev;
    InputDevice active_device = InputDevice::KEYBOARD_MOUSE;
    bool device_changed = false;

    bool captured = false;
    bool released_by_escape = false;
    RECT capture_rect{};

    InputActionState actions[(uint32_t)InputAction::COUNT];
    std::vector<BindingState> binding_states[(uint32_t)InputAction::COUNT];

    void attach(HWND p_hwnd);
    void detach();
    void set_default_bindings();
    bool handle_message(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam);
    void update(float p_dt);
    void capture(const RECT& p_client_rect);
    void release();
    void clear();

    const InputActionState& action(InputAction p_action) const { return actions[(uint32_t)p_action]; }
    bool down(InputAction p_action) const { return captured && action(p_action).down; }
    bool pressed(InputAction p_action) const { return captured && action(p_action).pressed; }
    vec2 axis(InputAction p_action) const { return captured ? action(p_action).value : vec2(0.0f); }

    void _set_key(uint32_t p_key, bool p_down);
    void _detect_device();
    void _evaluate(float p_dt);
    bool _source_down(const InputBinding& p_binding) const;
    vec2 _source_axis(const InputBinding& p_binding) const;
    void _apply_capture();
};

}
