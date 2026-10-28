#pragma once
#include <editor/layout/panel.h>
#include <memory>
#include <vector>

namespace lumen {

struct ContentBrowserPanel;
struct GpuProfilerPanel;
struct MemoryPanel;
struct OutputPanel;

struct SharedPanels
{
    std::vector<std::unique_ptr<Panel>> panels;
    ContentBrowserPanel* content_browser = nullptr;
    GpuProfilerPanel* gpu_profiler = nullptr;
    MemoryPanel* memory = nullptr;
    OutputPanel* output = nullptr;

    void initialize();
    void shutdown();

    template <class T>
    T* _add() {
        auto up = std::make_unique<T>();
        T* raw = up.get();
        panels.push_back(std::move(up));
        return raw;
    }
};

}
