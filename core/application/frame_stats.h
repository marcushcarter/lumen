#pragma once
#include <core/base/cpu_profiler.h>
#include <windows.h>
#include <cstdint>

namespace lumen {

struct FrameStats
{
    static constexpr uint32_t HISTORY = 256;
    static constexpr uint32_t MAX_ROWS = 32;
    static constexpr uint32_t MAX_WAITS = 64;
    static constexpr float RESOLVE_INTERVAL_S = 0.5f;

    struct Row
    {
        const char* name;
        bool wait;
        float samples[HISTORY];
        float avg;
        float peak;
    };

    Row rows[MAX_ROWS] = {};
    uint32_t row_count = 0;

    float frame_samples[HISTORY] = {};
    float wait_samples[HISTORY] = {};
    float frame_avg = 0.0f;
    float frame_peak = 0.0f;
    float wait_avg = 0.0f;

    uint32_t sample_head = 0;
    uint32_t sample_count = 0;
    uint64_t last_frame = UINT64_MAX;
    float resolve_timer = 0.0f;
    uint32_t refresh_hz = 0;
    bool visible = true;

    void initialize(HWND p_hwnd);
    void update(const CpuProfiler& p_profiler);

    uint32_t _row(const char* p_name, bool p_wait);
    void _resolve();
};

}