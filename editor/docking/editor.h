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

    static constexpr float MIN_DOCKWELL = 0.025f;
    static constexpr float MAX_DOCKWELL = 0.5f;
    static constexpr float MIN_WELL = 0.15f;
    static constexpr float CORNER_REACH = 10.0f;

    struct CornerState
    {
        bool hovered = false;
        bool active = false;
        bool activated = false;
        ImVec2 delta = ImVec2(0.0f, 0.0f);
    };

    CenterView center_view;
    DockWell right_top;
    DockWell right_bottom;

    std::vector<std::unique_ptr<Panel>> panels;

    float split_x = 0.8f;
    float split_y = 0.5f;
    bool right_collapsed = false;

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
    CornerState _corner_splitter(const char* p_id, ImVec2 p_cross, bool p_h_left, ImVec2 p_v_min, ImVec2 p_v_max, ImVec2 p_h_min, ImVec2 p_h_max);
    void _apply_split_x(float p_delta, bool p_activated, float p_body_w);
    void on_update(EditorContext& ctx, float p_dt);
    void draw_menu();

    void take_screenshot(EditorContext& ctx);
};

}
