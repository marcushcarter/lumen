#pragma once
#include <editor/application/editor_context.h>
#include <editor/layout/panel.h>
#include <imgui.h>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumen {

enum class DockZone : uint8_t { LEFT_TOP, LEFT_BOTTOM, RIGHT_TOP, RIGHT_BOTTOM, BOTTOM, COUNT };

const char* to_string(DockZone p_zone);
DockZone dock_zone_from_string(std::string_view p_name, DockZone p_fallback);

struct EditorLayout
{
    static constexpr float MIN_SIDE_PX = 72.0f;
    static constexpr float MIN_CENTER_PX = 120.0f;
    static constexpr size_t ZONES = (size_t)DockZone::COUNT;
    static constexpr size_t MAX_PER_WELL = 16;

    struct Slot {
        Panel* panel = nullptr;
        DockZone zone = DockZone::LEFT_TOP;
        bool open = true;
    };

    struct Move {
        Panel* panel = nullptr;
        DockZone zone = DockZone::LEFT_TOP;
    };

    std::vector<Slot> slots;
    std::vector<Slot> document_slots;
    std::unordered_map<std::string, DockZone> document_zones;
    bool document_locked = false;
    std::array<std::string, ZONES> active;
    std::function<void(EditorContext&, ImVec2, ImVec2)> center;

    float left_w = 300.0f;
    float right_w = 300.0f;
    float bottom_h = 260.0f;
    float left_split_y = 0.5f;
    float right_split_y = 0.5f;
    Move pending;
    ImVec2 area_min = ImVec2(0.0f, 0.0f);
    ImVec2 area_max = ImVec2(0.0f, 0.0f);

    Panel* close_request = nullptr;

    void add(Panel* p_panel, DockZone p_zone, bool p_open = true);
    void set_document(const std::vector<Slot>& p_slots, bool p_locked);
    DockZone document_zone(const Panel* p_panel, DockZone p_fallback) const;
    template <class F> void _each(F&& f) {
        for (Slot& s : slots) if (s.open) f(s);
        for (Slot& s : document_slots) if (s.open) f(s);
    }
    void open(Panel* p_panel);
    bool is_open(const Panel* p_panel) const;
    Slot* find(std::string_view p_name);
    bool has(DockZone p_zone) const;
    Panel* _active(DockZone p_zone);

    void _draw_well(EditorContext& ctx, DockZone p_zone, ImVec2 p_min, ImVec2 p_max);
    void _draw_column(EditorContext& ctx, DockZone p_top, DockZone p_bottom, float& r_split, ImVec2 p_min, ImVec2 p_max, const char* p_id);
    void draw(EditorContext& ctx, const std::function<void(EditorContext&, ImVec2, ImVec2)>& p_toolbar);
};

}
