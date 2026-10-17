#pragma once
#include <editor/drawer/drawer_tab.h>
#include <imgui.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace lumen {

enum class DrawerTabId : uint32_t { CONTENT_BROWSER, PROFILER, MEMORY, OUTPUT, COUNT };

constexpr uint32_t drawer_bit(DrawerTabId p_id) { return 1u << (uint32_t)p_id; }

struct DrawerState {
    uint32_t mask = UINT32_MAX;
    int active = 0;
    bool collapsed = true;
};

struct Drawer
{
    std::vector<std::unique_ptr<DrawerTab>> tabs;
    char console_buffer[512] = {};

    void initialize();
    void shutdown();
    void draw_content(EditorContext& ctx, DrawerState& r_state);
    void draw_strip(ImVec2 p_min, ImVec2 p_max, DrawerState& r_state);

    bool _visible(const DrawerState& p_state, int p_index) const;
    void _resolve_active(DrawerState& r_state) const;
};

}