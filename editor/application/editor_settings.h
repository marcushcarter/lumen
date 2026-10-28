#pragma once
#include <core/base/vsync_mode.h>
#include <core/base/error.h>
#include <imgui.h>
#include <string_view>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }

struct Theme
{
    struct ThemePreset { const char* name; ImVec4 base, accent, text; };

    ImVec4 base { 0.01f, 0.01f, 0.01f, 1.0f };
    ImVec4 accent { 0.06f, 0.06f, 0.78f, 1.0f };
    ImVec4 text { 0.92f, 0.92f, 0.92f, 1.0f };
    bool use_system_accent = false;
    int preset = -1;

    void apply() const;

    static inline constexpr ThemePreset THEME_PRESETS[] = {
        { "Default", { 0.01f, 0.01f, 0.01f, 1.0f }, { 0.06f, 0.06f, 0.78f, 1.0f }, { 0.92f, 0.92f, 0.92f, 1.0f } },
        { "Dark", { 0.01f, 0.01f, 0.01f, 1.0f }, { 0.06f, 0.06f, 0.78f, 1.0f }, { 0.92f, 0.92f, 0.92f, 1.0f } },
        { "Light", { 0.90f, 0.90f, 0.92f, 1.0f }, { 0.6f, 0.6f, 0.6f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { "Classic ImGui", { 0.40f, 0.40f, 0.40f, 1.0f }, { 0.26f, 0.59f, 0.98f, 1.0f }, { 1.00f, 1.00f, 1.00f, 1.0f } }
    };

    static uint32_t to_colorref(const ImVec4& p_color);
    static const char* theme_preset_name(int i);
    static int theme_preset_index(std::string_view n);
};

struct EditorSettings
{
    Theme theme;
    VsyncMode vsync_mode = VsyncMode::FAST;
    int fps_cap = 120;
    bool dirty = false;
};

}
