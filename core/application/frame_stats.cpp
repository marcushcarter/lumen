#include <core/application/frame_stats.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void FrameStats::initialize(HWND p_hwnd)
{
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(MonitorFromWindow(p_hwnd, MONITOR_DEFAULTTONEAREST), &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        refresh_hz = dm.dmDisplayFrequency;
}

uint32_t FrameStats::_row(const char* p_name, bool p_wait)
{
    for (uint32_t i = 0; i < row_count; i++) if (rows[i].name == p_name) return i;
    if (row_count == MAX_ROWS) return MAX_ROWS;
    Row& r = rows[row_count];
    r = {};
    r.name = p_name;
    r.wait = p_wait;
    return row_count++;
}

void FrameStats::update(const CpuProfiler& p_profiler)
{
    const CpuProfiler::Frame* f = p_profiler.last_complete_frame();
    if (!f || f->number == last_frame) return;
    last_frame = f->number;

    float row_ms[MAX_ROWS] = {};
    int64_t wait_begin[MAX_WAITS];
    int64_t wait_end[MAX_WAITS];
    uint32_t wait_count = 0;

    for (const CpuProfiler::Zone& z : p_profiler.zones) {
        if (z.lane != p_profiler.main_lane || z.begin < f->begin || z.begin >= f->end) continue;
        const bool wait = (z.flags & CpuProfiler::FLAG_WAIT) != 0;
        if (wait && wait_count < MAX_WAITS) {
            wait_begin[wait_count] = z.begin;
            wait_end[wait_count] = z.end;
            wait_count++;
        }
        if (z.depth != 0) continue;
        const uint32_t r = _row(z.name, wait);
        if (r < MAX_ROWS) row_ms[r] += (float)((double)(z.end - z.begin) * p_profiler.ticks_to_ms);
    }

    int64_t wait_ticks = 0;
    if (wait_count) {
        uint32_t order[MAX_WAITS];
        for (uint32_t i = 0; i < wait_count; i++) order[i] = i;
        std::sort(order, order + wait_count, [&](uint32_t a, uint32_t b) { return wait_begin[a] < wait_begin[b]; });
        int64_t cur_b = wait_begin[order[0]];
        int64_t cur_e = wait_end[order[0]];
        for (uint32_t i = 1; i < wait_count; i++) {
            const uint32_t w = order[i];
            if (wait_begin[w] <= cur_e) { cur_e = std::max(cur_e, wait_end[w]); continue; }
            wait_ticks += cur_e - cur_b;
            cur_b = wait_begin[w];
            cur_e = wait_end[w];
        }
        wait_ticks += cur_e - cur_b;
    }

    const float frame_ms = (float)((double)(f->end - f->begin) * p_profiler.ticks_to_ms);
    frame_samples[sample_head] = frame_ms;
    wait_samples[sample_head] = (float)((double)wait_ticks * p_profiler.ticks_to_ms);
    for (uint32_t i = 0; i < row_count; i++) rows[i].samples[sample_head] = row_ms[i];
    sample_head = (sample_head + 1) % HISTORY;
    if (sample_count < HISTORY) sample_count++;

    resolve_timer += frame_ms * 0.001f;
    if (resolve_timer >= RESOLVE_INTERVAL_S) {
        resolve_timer = 0.0f;
        _resolve();
    }
}

void FrameStats::_resolve()
{
    if (!sample_count) return;
    const float inv = 1.0f / (float)sample_count;

    frame_avg = frame_peak = wait_avg = 0.0f;
    for (uint32_t s = 0; s < sample_count; s++) {
        frame_avg += frame_samples[s];
        wait_avg += wait_samples[s];
        frame_peak = std::max(frame_peak, frame_samples[s]);
    }
    frame_avg *= inv;
    wait_avg *= inv;

    for (uint32_t i = 0; i < row_count; i++) {
        Row& r = rows[i];
        r.avg = r.peak = 0.0f;
        for (uint32_t s = 0; s < sample_count; s++) {
            r.avg += r.samples[s];
            r.peak = std::max(r.peak, r.samples[s]);
        }
        r.avg *= inv;
    }
}

}