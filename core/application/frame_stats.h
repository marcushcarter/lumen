// core/application/frame_stats.h
#pragma once
#include <windows.h>
#include <cstdint>

namespace lumen {

struct FrameStats
{
    enum class Zone : uint32_t { Poll, Swapchain, ImGuiBegin, Camera, GpuWait, BeginFrame, GraphBuild, Update, Overlay, ImGuiRender, Record, SubmitPresent, EndFrame, Count };

    static constexpr uint32_t ZONE_COUNT = (uint32_t)Zone::Count;
    static constexpr uint32_t HISTORY = 256;
    static constexpr float RESOLVE_INTERVAL_S = 0.5f;
    static constexpr const char* ZONE_NAMES[ZONE_COUNT] = {
        "poll events", "swapchain update", "imgui begin", "camera", "wait gpu + acquire", "frame build + upload",
        "graph build + compile", "on_update", "stats overlay", "imgui render", "record", "submit + present", "end frame"
    };

    struct Sample
    {
        float zone_ms[ZONE_COUNT];
        float frame_ms;
    };

    double ticks_to_ms = 0.0;
    int64_t frame_start = 0;
    int64_t lap_start = 0;

    Sample current{};
    Sample samples[HISTORY]{};
    uint32_t sample_head = 0;
    uint32_t sample_count = 0;

    Sample avg{};
    Sample peak{};
    float resolve_timer = 0.0f;
    uint32_t refresh_hz = 0;
    bool visible = true;

    void initialize(HWND p_hwnd);
    void begin_frame();
    void lap(Zone p_zone);
    void draw();

    void _resolve();
    static int64_t _now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
};

}