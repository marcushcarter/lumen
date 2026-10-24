#pragma once
#include <editor/drawer/drawer_tab.h>
#include <core/rendering/gpu_profiler.h>
#include <IconsFontAwesome6.h>
#include <cstdint>

namespace lumen {
    
struct ProfilerDrawerTab : DrawerTab
{
    uint64_t sel_pass_key = 0;
    uint64_t sel_draw_key = 0;
    
    const GpuProfiler::Timing* selected_pass = nullptr;
    const GpuProfiler::Timing* selected_draw = nullptr;

    char sel_name[64] = {};
    bool follow = false;

    struct ScrollBuf {
        int max = 256;
        ImVector<float> data;
        void add(float v) {
            data.push_back(v);
            if (data.Size > max) data.erase(data.Data);
        }
        void clear() { data.shrink(0); }
    };
    
    uint64_t plot_key = 0;
    ScrollBuf plot_hist;
    std::vector<float> sort_scratch;

    const char* name() const override { return "GPU Profiler"; }
    void draw(EditorContext& ctx) override;

    void _draw_info(EditorContext& ctx, bool frozen);
    void _draw_timeline(EditorContext& ctx);
    void _copy_report(EditorContext& ctx);
};

}