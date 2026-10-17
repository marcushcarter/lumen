#pragma once
#include <editor/editor_context.h>
#include <editor/layout/dock_well.h>
#include <editor/layout/panel.h>
#include <editor/drawer/drawer.h>
#include <editor/viewport/viewport.h>
#include <imgui.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct EditorLayout
{
    static constexpr float MIN_DOCKWELL = 0.025f;
    static constexpr float MAX_DOCKWELL = 0.5f;
    static constexpr float MIN_WELL = 0.15f;
    static constexpr float MIN_DRAWER = 0.025f;
    static constexpr float MAX_DRAWER = 0.7f;
    static constexpr float CORNER_REACH = 10.0f;

    struct CornerState {
        bool hovered = false;
        bool active = false;
        bool activated = false;
        ImVec2 delta = ImVec2(0.0f, 0.0f);
    };

    std::string host_id;
    Viewport viewport;
    DrawerState drawer;
    DockWell right_top;
    DockWell right_bottom;
    std::vector<std::unique_ptr<Panel>> panels;

    float split_x = 0.8f;
    float split_y = 0.5f;
    float center_split = 1.0f;
    bool right_collapsed = false;

    float _usable_h = 1.0f;
    float _scene_h = 0.0f;
    float _drawer_h = 0.0f;

    template <class T, class... A>
    T* add_panel(A&&... a) {
        auto up = std::make_unique<T>(std::forward<A>(a)...);
        up->zone = up->default_zone();
        T* raw = up.get();
        panels.push_back(std::move(up));
        return raw;
    }

    void initialize(const char* p_host_id);
    void shutdown();
    void draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar);
    void draw_menu();

    float _layout_center(ImVec2 p_min, ImVec2 p_max);
    void _apply_center_delta(float p_delta, bool p_activated);
    void _draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    CornerState _corner_splitter(const char* p_id, ImVec2 p_cross, bool p_h_left, ImVec2 p_v_min, ImVec2 p_v_max, ImVec2 p_h_min, ImVec2 p_h_max);
    void _apply_split_x(float p_delta, bool p_activated, float p_body_w);
};

}