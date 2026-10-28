#pragma once
#include <editor/application/editor_context.h>
#include <editor/layout/dock_well.h>
#include <editor/layout/panel.h>
#include <editor/viewport/viewport.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct EditorLayout
{
    static constexpr float MIN_DOCKWELL = 0.025f;
    static constexpr float MAX_DOCKWELL = 0.5f;
    static constexpr float MIN_CENTER = 0.2f;
    static constexpr float MIN_WELL = 0.15f;
    static constexpr float MIN_DRAWER = 0.1f;
    static constexpr float MAX_DRAWER = 0.7f;
    static constexpr float OPEN_SPLIT = 0.5f;
    static constexpr float CORNER_REACH = 10.0f;
    static constexpr float DEFAULT_SPLIT_LEFT = 0.2f;
    static constexpr float DEFAULT_SPLIT_RIGHT = 0.8f;

    struct CornerState {
        bool hovered = false;
        bool active = false;
        bool activated = false;
        ImVec2 delta = ImVec2(0.0f, 0.0f);
    };

    struct Column {
        DockWell top;
        DockWell bottom;
        float split_y = 0.5f;
        bool collapsed = false;
    };

    struct Drop {
        Panel* panel = nullptr;
        DockZone zone = DockZone::BOTTOM;
    };

    std::string host_id;
    Viewport viewport;
    DockWell bottom;
    Column left;
    Column right;
    std::vector<std::unique_ptr<Panel>> owned;
    std::vector<DockSlot> slots;

    float split_left = DEFAULT_SPLIT_LEFT;
    float split_right = DEFAULT_SPLIT_RIGHT;
    float center_split = 1.0f;
    char console_buffer[512] = {};

    float usable_h = 1.0f;
    float scene_h = 0.0f;
    float bottom_h = 0.0f;
    float drawer_drag_start = 1.0f;
    bool drawer_dragging = false;
    bool bottom_was_open = false;
    Drop pending_drop;

    float _layout_center(float p_top, float p_bottom);
    void _apply_center_delta(float p_delta, bool p_activated);
    void _end_drawer_drag();
    void _draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_column(EditorContext& ctx, Column& r_column, bool p_dragging, ImVec2 p_min, ImVec2 p_max);
    void _column_layout(Column& r_column, bool p_dragging, float p_col_h, bool& r_top, bool& r_bottom, float& r_top_h);
    CornerState _corner_splitter(const char* p_id, ImVec2 p_cross, bool p_h_left, ImVec2 p_v_min, ImVec2 p_v_max, ImVec2 p_h_min, ImVec2 p_h_max);
    void _apply_split_left(float p_delta, bool p_activated, float p_body_w);
    void _apply_split_right(float p_delta, bool p_activated, float p_body_w);
    void _edge_target(const char* p_id, ImVec2 p_min, DockZone p_zone);
    void _draw_footer(ImVec2 p_min, ImVec2 p_max);
    void _uncollapse(DockZone p_zone);
    void _apply_drop();

    template <class T, class... A>
    T* add_panel(A&&... a) {
        auto up = std::make_unique<T>(std::forward<A>(a)...);
        T* raw = up.get();
        owned.push_back(std::move(up));
        add_shared(raw);
        return raw;
    }

    void add_shared(Panel* p_panel);
    DockSlot* find(const char* p_name);
    DockSlot* find(const Panel* p_panel);

    template <class T>
    DockSlot* find() {
        for (DockSlot& s : slots) if (dynamic_cast<T*>(s.panel)) return &s;
        return nullptr;
    }

    template <class T>
    T* open() {
        DockSlot* s = find<T>();
        if (!s) return nullptr;
        open_panel(*s);
        return static_cast<T*>(s->panel);
    }
    DockWell& well(DockZone p_zone);
    std::array<DockWell*, 5> wells() { return { &bottom, &left.top, &left.bottom, &right.top, &right.bottom }; }
    void open_panel(DockSlot& r_slot);

    void initialize(const char* p_host_id);
    void shutdown();
    void draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar);
    void draw_menu();
};

}
