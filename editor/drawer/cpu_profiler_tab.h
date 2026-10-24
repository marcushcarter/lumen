#pragma once
#include <editor/drawer/profiler_base.h>
#include <cstdint>
#include <vector>

namespace lumen {

// One CPU frame of real time, 0 ms = its start: one lane per thread, zones clipped to the frame.
struct CpuProfilerDrawerTab : ProfilerTabBase
{
    struct Capture {
        struct Zone { const char* name; float begin_ms; float end_ms; uint16_t lane; uint8_t depth; uint8_t flags; };
        struct Lane { char name[32]; uint32_t index; uint32_t rows; };

        uint64_t frame = UINT64_MAX;
        float total_ms = 0.0f;
        std::vector<Zone> zones;
        std::vector<Lane> lanes;
    };

    Capture capture;
    std::vector<float> lane_y;

    const char* name() const override { return "CPU Profiler"; }

    void _draw_graph(EditorContext& ctx) override;

    void _capture_update();
};

}
