#pragma once
#include <editor/drawer/drawer_tab.h>
#include <imgui.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace lumen {

struct DrawerState
{
    std::vector<DrawerTab*> open_tabs;
    DrawerTab* active = nullptr;

    bool is_open(const DrawerTab* p_tab) const;
    void open(DrawerTab* p_tab);
    void close(DrawerTab* p_tab);
    void toggle(DrawerTab* p_tab);
};

struct Drawer
{
    std::vector<std::unique_ptr<DrawerTab>> tabs;
    char console_buffer[512] = {};

    void initialize();
    void shutdown();
    DrawerTab* find(const char* p_name) const;
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max, DrawerState& r_state);
    void draw_bar(ImVec2 p_min, ImVec2 p_max, DrawerState& r_state);
};

}