#pragma once
#include <core/base/error.h>
#include <editor/editor_context.h>
#include <editor/docking/center_view/center_view.h>
#include <editor/docking/dock_well.h>
#include <editor/docking/panel.h>
#include <memory>
#include <vector>

namespace lumen {

struct Editor
{
    static constexpr int VERSION = 1;

    static constexpr float PLAY_MARGIN = 6.0f;
    static constexpr float PLAY_INSET = 2.0f;
    static constexpr float PLAY_ROUNDING = 3.0f;
    static constexpr float PLAY_KEBAB_W = 14.0f;
    static constexpr float PLAY_KEBAB_GAP = 4.0f;
    static constexpr ImU32 PLAY_GREEN = IM_COL32(29, 124, 18, 255);
    static constexpr ImU32 PLAY_DISABLED_ALPHA = 0x60;

    CenterView center_view;
    DockWell right_top;
    DockWell right_bottom;

    std::vector<std::unique_ptr<Panel>> panels;

    float split_x = 0.8f;
    float split_y = 0.5f;
    bool right_collapsed = false;
    float bar_h = 36.0f;

    template <class T, class... A>
    T* add_panel(A&&... a) {
        auto up = std::make_unique<T>(std::forward<A>(a)...);
        up->zone = up->default_zone();
        T* raw = up.get();
        panels.push_back(std::move(up));
        return raw;
    }

    Error initialize();
    void shutdown();
    
    void _draw_toolbar(EditorContext& ctx);
    void _draw_play_controls(EditorContext& ctx, ImVec2 p_bar_min, ImVec2 p_bar_max);
    void on_update(EditorContext& ctx, float p_dt);
    void draw_menu();

    void take_screenshot(EditorContext& ctx);
};

}
