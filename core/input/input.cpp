#include <core/input/input.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>

namespace lumen {

static constexpr USHORT HID_USAGE_PAGE_GENERIC = 0x01;
static constexpr USHORT HID_USAGE_GENERIC_MOUSE = 0x02;

static vec2 _deadzone(vec2 p_stick)
{
    const float len = length(p_stick);
    if (len <= Input::STICK_DEADZONE) return vec2(0.0f);
    const float scaled = std::min((len - Input::STICK_DEADZONE) / (1.0f - Input::STICK_DEADZONE), 1.0f);
    return p_stick / len * scaled;
}

void Input::attach(HWND p_hwnd)
{
    hwnd = p_hwnd;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_MOUSE;
    rid.dwFlags = 0;
    rid.hwndTarget = p_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    set_default_bindings();
}

void Input::detach()
{
    release();
    hwnd = nullptr;
}

void Input::set_default_bindings()
{
    for (InputActionState& a : actions) a = InputActionState{};
    auto bind = [&](InputAction p_action, InputActionType p_type, std::initializer_list<InputBinding> p_bindings) {
        InputActionState& a = actions[(uint32_t)p_action];
        a.type = p_type;
        a.bindings = p_bindings;
        binding_states[(uint32_t)p_action].assign(a.bindings.size(), BindingState{});
    };
    auto key = [](uint16_t p_key, InputTrigger p_trigger = InputTrigger::HOLD) { return InputBinding{ InputSource::KEY, p_trigger, { p_key, 0, 0, 0 } }; };
    auto pad = [](GamepadButton p_button, InputTrigger p_trigger = InputTrigger::HOLD) { return InputBinding{ InputSource::GAMEPAD_BUTTON, p_trigger, { (uint16_t)p_button, 0, 0, 0 } }; };

    bind(InputAction::MOVE, InputActionType::AXIS, { InputBinding{ InputSource::KEYS_2D, InputTrigger::HOLD, { 'W', 'S', 'A', 'D' } }, InputBinding{ InputSource::GAMEPAD_LEFT_STICK } });
    bind(InputAction::LOOK, InputActionType::AXIS, { InputBinding{ InputSource::MOUSE_DELTA }, InputBinding{ InputSource::GAMEPAD_RIGHT_STICK } });
    bind(InputAction::RUN, InputActionType::BUTTON, { key(VK_SHIFT), pad(GamepadButton::LEFT_STICK, InputTrigger::TOGGLE) });
    bind(InputAction::WALK, InputActionType::BUTTON, { key(VK_CONTROL, InputTrigger::TOGGLE) });
    bind(InputAction::JUMP, InputActionType::BUTTON, { key(VK_SPACE), pad(GamepadButton::A) });
}

void Input::_set_key(uint32_t p_key, bool p_down)
{
    if (p_key < SOURCE_COUNT) keys[p_key] = p_down;
}

bool Input::handle_message(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam)
{
    if (p_hwnd != hwnd) return false;
    switch (p_msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (p_wparam == VK_ESCAPE && captured) {
                release();
                released_by_escape = true;
            }
            _set_key((uint32_t)p_wparam, true);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            _set_key((uint32_t)p_wparam, false);
            break;
        case WM_LBUTTONDOWN: _set_key(MOUSE_LEFT, true); break;
        case WM_LBUTTONUP: _set_key(MOUSE_LEFT, false); break;
        case WM_RBUTTONDOWN: _set_key(MOUSE_RIGHT, true); break;
        case WM_RBUTTONUP: _set_key(MOUSE_RIGHT, false); break;
        case WM_MBUTTONDOWN: _set_key(MOUSE_MIDDLE, true); break;
        case WM_MBUTTONUP: _set_key(MOUSE_MIDDLE, false); break;
        case WM_XBUTTONDOWN: _set_key(GET_XBUTTON_WPARAM(p_wparam) == XBUTTON1 ? MOUSE_X1 : MOUSE_X2, true); break;
        case WM_XBUTTONUP: _set_key(GET_XBUTTON_WPARAM(p_wparam) == XBUTTON1 ? MOUSE_X1 : MOUSE_X2, false); break;
        case WM_INPUT: {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(p_lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) break;
            if (raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                mouse_delta_pending += vec2((float)raw.data.mouse.lLastX, (float)raw.data.mouse.lLastY);
            }
            break;
        }
        case WM_KILLFOCUS:
            clear();
            release();
            break;
        case WM_ACTIVATE:
            if (LOWORD(p_wparam) == WA_INACTIVE) {
                clear();
                release();
            }
            break;
        default:
            break;
    }
    return false;
}

void Input::clear()
{
    std::memset(keys, 0, sizeof(keys));
    mouse_delta_pending = vec2(0.0f);
}

void Input::capture(const RECT& p_client_rect)
{
    if (!hwnd) return;
    capture_rect = p_client_rect;
    captured = true;
    released_by_escape = false;
    mouse_delta_pending = vec2(0.0f);
    _apply_capture();
}

void Input::release()
{
    if (!captured) return;
    captured = false;
    ClipCursor(nullptr);
}

void Input::_apply_capture()
{
    if (!captured || !hwnd) return;
    POINT lo{ capture_rect.left, capture_rect.top };
    POINT hi{ capture_rect.right, capture_rect.bottom };
    ClientToScreen(hwnd, &lo);
    ClientToScreen(hwnd, &hi);
    const RECT clip{ lo.x, lo.y, hi.x, hi.y };
    ClipCursor(&clip);
    SetCursorPos((lo.x + hi.x) / 2, (lo.y + hi.y) / 2);
}

void Input::_detect_device()
{
    bool keyboard_mouse = length(mouse_delta) > 0.0f;
    for (uint32_t i = 0; i < SOURCE_COUNT && !keyboard_mouse; i++) keyboard_mouse = keys[i] != keys_prev[i];

    bool pad = false;
    if (gamepad.connected) {
        for (uint32_t i = 0; i < (uint32_t)GamepadButton::COUNT && !pad; i++) pad = gamepad.buttons[i] != gamepad_prev.buttons[i];
        pad = pad || length(gamepad.left_stick) > DEVICE_SWITCH_THRESHOLD || length(gamepad.right_stick) > DEVICE_SWITCH_THRESHOLD;
        pad = pad || gamepad.left_trigger > DEVICE_SWITCH_THRESHOLD || gamepad.right_trigger > DEVICE_SWITCH_THRESHOLD;
    }

    const InputDevice before = active_device;
    if (pad) active_device = InputDevice::GAMEPAD;
    else if (keyboard_mouse) active_device = InputDevice::KEYBOARD_MOUSE;
    if (!gamepad.connected) active_device = InputDevice::KEYBOARD_MOUSE;
    device_changed = active_device != before;
}

bool Input::_source_down(const InputBinding& p_binding) const
{
    switch (p_binding.source) {
        case InputSource::KEY: return p_binding.keys[0] < SOURCE_COUNT && keys[p_binding.keys[0]];
        case InputSource::GAMEPAD_BUTTON: return gamepad.connected && p_binding.keys[0] < (uint32_t)GamepadButton::COUNT && gamepad.buttons[p_binding.keys[0]];
        default: return false;
    }
}

vec2 Input::_source_axis(const InputBinding& p_binding) const
{
    switch (p_binding.source) {
        case InputSource::KEYS_2D: {
            auto held = [&](uint16_t p_key) { return p_key < SOURCE_COUNT && keys[p_key] ? 1.0f : 0.0f; };
            const vec2 v(held(p_binding.keys[3]) - held(p_binding.keys[2]), held(p_binding.keys[0]) - held(p_binding.keys[1]));
            const float len = length(v);
            return len > 1.0f ? v / len : v;
        }
        case InputSource::MOUSE_DELTA: return mouse_delta;
        case InputSource::GAMEPAD_LEFT_STICK: return gamepad.connected ? _deadzone(gamepad.left_stick) : vec2(0.0f);
        case InputSource::GAMEPAD_RIGHT_STICK: return gamepad.connected ? _deadzone(gamepad.right_stick) : vec2(0.0f);
        default: return vec2(0.0f);
    }
}

void Input::_evaluate(float p_dt)
{
    for (uint32_t ai = 0; ai < (uint32_t)InputAction::COUNT; ai++) {
        InputActionState& a = actions[ai];
        std::vector<BindingState>& states = binding_states[ai];
        if (states.size() != a.bindings.size()) states.assign(a.bindings.size(), BindingState{});
        const bool was_down = a.down;
        bool down = false;
        vec2 value(0.0f);

        for (size_t i = 0; i < a.bindings.size(); i++) {
            const InputBinding& b = a.bindings[i];
            BindingState& s = states[i];
            if (a.type == InputActionType::AXIS) {
                const vec2 v = captured ? _source_axis(b) : vec2(0.0f);
                if (v != vec2(0.0f)) a.device = b.device();
                value += v;
                continue;
            }

            const bool raw = captured && _source_down(b);
            const bool rose = raw && !s.raw;
            const bool fell = !raw && s.raw;
            s.raw = raw;
            bool out = false;
            switch (b.trigger) {
                case InputTrigger::HOLD:
                    out = raw;
                    break;
                case InputTrigger::TOGGLE:
                    if (rose) s.latched = !s.latched;
                    if (!captured) s.latched = false;
                    out = s.latched;
                    break;
                case InputTrigger::TAP:
                    if (rose) s.held = 0.0f;
                    if (raw) s.held += p_dt;
                    out = fell && s.held <= TAP_TIME;
                    break;
                case InputTrigger::DOUBLE_TAP:
                    s.since_release += p_dt;
                    if (rose) {
                        if (s.taps == 1 && s.since_release <= DOUBLE_TAP_TIME) s.latched = true;
                        else s.taps = 1;
                    }
                    if (fell) {
                        s.since_release = 0.0f;
                        if (s.latched) {
                            s.latched = false;
                            s.taps = 0;
                        }
                    }
                    out = raw && s.latched;
                    break;
            }
            if (out) {
                down = true;
                a.device = b.device();
            }
        }

        a.value = a.type == InputActionType::AXIS ? value : vec2(down ? 1.0f : 0.0f, 0.0f);
        a.down = a.type == InputActionType::AXIS ? value != vec2(0.0f) : down;
        a.pressed = a.down && !was_down;
        a.released = !a.down && was_down;
    }
}

void Input::update(float p_dt)
{
    mouse_delta = captured ? mouse_delta_pending : vec2(0.0f);
    mouse_delta_pending = vec2(0.0f);
    _detect_device();
    _evaluate(p_dt);
    std::memcpy(keys_prev, keys, sizeof(keys));
    gamepad_prev = gamepad;
    _apply_capture();
}

}
