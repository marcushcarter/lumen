#pragma once
#include <editor/docking/center_view/debug_tab.h>
#include <editor/docking/center_view/profiler/profiler_timeline.h>
#include <editor/docking/center_view/profiler/profiler_distribution.h>
#include <editor/docking/center_view/profiler/profiler_resources.h>
#include <IconsFontAwesome6.h>
#include <cstdint>

namespace lumen {
    
struct ProfilerDebugTab : DebugTab
{
    ProfilerTimeline timeline;
    ProfilerDistribution distribution;
    ProfilerResources resources;

    const char* name() const override { return ICON_FA_ARROW_TREND_UP " Profiler"; }
    void draw(EditorContext& ctx) override;
};

}