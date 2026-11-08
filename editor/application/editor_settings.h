#pragma once
#include <core/base/vsync_mode.h>
#include <core/base/error.h>
#include <imgui.h>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }

struct Theme
{
    void apply() const;

    static uint32_t to_colorref(const ImVec4& p_color);
};

struct EditorSettings
{
    Theme theme;
    VsyncMode vsync_mode = VsyncMode::FAST;
    int fps_cap = 120;
    bool reopen_last_project = true;
    bool dirty = false;
};

}
