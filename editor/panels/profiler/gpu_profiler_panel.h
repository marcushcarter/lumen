#pragma once
#include <editor/panels/profiler/profiler_panel_base.h>
#include <core/rendering/gpu_profiler.h>
#include <cstdint>
#include <vector>

namespace lumen {

// One GPU frame, 0 ms = its first timestamp: categories, passes, barriers, draws/dispatches.
struct GpuProfilerPanel : ProfilerPanelBase
{
    std::vector<float> start;

    const char* name() const override { return "GPU Profiler"; }

    void _draw_toolbar(EditorContext& ctx) override;
    void _draw_graph(EditorContext& ctx) override;

    void _copy_report(EditorContext& ctx);
};

}
