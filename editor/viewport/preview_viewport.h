#pragma once
#include <core/rendering/debug_draw.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <editor/application/editor_context.h>
#include <imgui.h>
#include <functional>

namespace lumen {

struct PreviewViewport
{
    static constexpr float LOOK_CLICK_SLOP = 3.0f;

    drivers::CursorLockWin32 cursor_lock;
    std::function<void(DebugDraw&)> debug_lines;

    bool allow_grid = true;

    bool looking = false;
    ImVec2 look_delta = ImVec2(0.0f, 0.0f);
    float look_zoom = 0.0f;
    ImVec2 _look_press = ImVec2(0.0f, 0.0f);
    float _look_travel = 0.0f;

    void _update_look(bool p_start, ImVec2 p_center);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void release();
};

}
