#pragma once
#include <core/base/vsync_mode.h>
#include <core/base/error.h>
#include <imgui.h>
#include <string>
#include <iterator>
#include <cstdio>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }

struct Theme
{
    ImVec4 base { 0.01f, 0.01f, 0.01f, 1.0f };
    ImVec4 accent { 0.06f, 0.06f, 0.78f, 1.0f };
    ImVec4 text { 0.92f, 0.92f, 0.92f, 1.0f };
    bool use_system_accent = false;
    int preset = -1;

    void apply() const;
    
    struct ThemePreset { const char* name; ImVec4 base, accent, text; };

    /**
        ADD THEMES.CFG FILE TO EASILY EDIT THEMES AND IMPORT THEM
        
        [Theme]
        Name=Default
        Base=0.012,0.010,0.014,1.000
        Accent=0.660,0.300,0.760,1.000

        [Theme]
        Name=Graphite
        Base=0.140,0.140,0.150,1.000
        Accent=0.350,0.550,0.850,1.000

        [Theme]
        Name=Midnight
        Base=0.080,0.090,0.130,1.000
        Accent=0.300,0.700,0.850,1.000
     */

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