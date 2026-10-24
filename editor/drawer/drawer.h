#pragma once
#include <editor/drawer/drawer_tab.h>
#include <imgui.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace lumen {

struct ContentBrowserDrawerTab;
struct GpuProfilerDrawerTab;
struct CpuProfilerDrawerTab;
struct MemoryDrawerTab;
struct OutputDrawerTab;

struct DrawerState
{
    std::vector<DrawerTab*> allowed;
    std::vector<DrawerTab*> open_tabs;
    DrawerTab* active = nullptr;

    bool allows(const DrawerTab* p_tab) const;
    bool is_open(const DrawerTab* p_tab) const;
    void open(DrawerTab* p_tab);
    void close(DrawerTab* p_tab);
    void toggle(DrawerTab* p_tab);
    void close_all();
};

struct Drawer
{
    std::vector<std::unique_ptr<DrawerTab>> tabs;
    ContentBrowserDrawerTab* content_browser = nullptr;
    GpuProfilerDrawerTab* gpu_profiler = nullptr;
    CpuProfilerDrawerTab* cpu_profiler_ = nullptr;
    MemoryDrawerTab* memory = nullptr;
    OutputDrawerTab* output = nullptr;
    char console_buffer[512] = {};

    void initialize();
    void shutdown();
    DrawerTab* find(const char* p_name) const;
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max, DrawerState& r_state);
    void draw_bar(ImVec2 p_min, ImVec2 p_max, DrawerState& r_state);

    template <class T>
    T* _add() {
        auto up = std::make_unique<T>();
        T* raw = up.get();
        tabs.push_back(std::move(up));
        return raw;
    }
};

}