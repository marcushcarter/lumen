#include <core/base/cpu_profiler.h>
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lumen {

static thread_local CpuProfiler::Lane* t_lane = nullptr;

CpuProfiler& cpu_profiler()
{
    static CpuProfiler instance;
    return instance;
}

int64_t CpuProfiler::now()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void CpuProfiler::initialize()
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    ticks_to_ms = 1000.0 / (double)freq.QuadPart;
    ticks_per_ns = (double)freq.QuadPart / 1.0e9;

    zones.reserve(8192);

    Lane* main = _lane();
    if (main) {
        main_lane = main->index;
        set_thread_name("Main");
    }
}

void CpuProfiler::shutdown()
{
    std::lock_guard lock(lane_mutex);
    const uint32_t n = lane_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; i++) {
        delete lanes[i];
        lanes[i] = nullptr;
    }
    lane_count.store(0, std::memory_order_release);
    t_lane = nullptr;
    zones.clear();
    frame_total = 0;
}

CpuProfiler::Lane* CpuProfiler::_lane()
{
    if (t_lane) return t_lane;

    std::lock_guard lock(lane_mutex);
    const uint32_t idx = lane_count.load(std::memory_order_relaxed);
    if (idx >= MAX_LANES) return nullptr;

    Lane* l = new Lane();
    l->index = (uint16_t)idx;
    l->depth = 0;
    std::snprintf(l->name, sizeof(l->name), "Thread %u", idx);
    l->pending.reserve(1024);

    lanes[idx] = l;
    lane_count.store(idx + 1, std::memory_order_release);
    t_lane = l;
    return l;
}

void CpuProfiler::set_thread_name(const char* p_name)
{
    Lane* l = _lane();
    if (l) std::snprintf(l->name, sizeof(l->name), "%s", p_name);
}

void CpuProfiler::zone_begin(const char* p_name, uint8_t p_flags)
{
    Lane* l = _lane();
    if (!l) return;
    const uint32_t d = l->depth++;
    if (d >= MAX_DEPTH) return;
    l->stack_name[d] = p_name;
    l->stack_flags[d] = p_flags;
    l->stack_begin[d] = now();
}

void CpuProfiler::zone_end()
{
    Lane* l = t_lane;
    if (!l || l->depth == 0) return;
    const uint32_t d = --l->depth;
    if (d >= MAX_DEPTH || !enabled.load(std::memory_order_relaxed)) return;

    const Zone z{ l->stack_name[d], l->stack_begin[d], now(), l->index, (uint8_t)d, l->stack_flags[d] };
    std::lock_guard lock(l->mutex);
    l->pending.push_back(z);
}

void CpuProfiler::begin_frame(uint64_t p_number)
{
    const int64_t t = now();

    Frame& cur = frames[frame_head];
    if (frame_total == 0 || cur.number != p_number) {
        if (frame_total) cur.end = t;
        frame_head = (frame_head + 1) % FRAME_HISTORY;
        frames[frame_head] = { p_number, t, 0 };
        frame_total++;
    }

    const uint32_t n = lane_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; i++) {
        Lane* l = lanes[i];
        std::lock_guard lock(l->mutex);
        zones.insert(zones.end(), l->pending.begin(), l->pending.end());
        l->pending.clear();
    }

    if (frame_total >= FRAME_HISTORY) {
        const int64_t cutoff = frames[(frame_head + 1) % FRAME_HISTORY].begin;
        std::erase_if(zones, [cutoff](const Zone& z) { return z.end < cutoff; });
    }
}

const CpuProfiler::Frame* CpuProfiler::frame(uint64_t p_number) const
{
    const uint32_t stored = (uint32_t)std::min<uint64_t>(frame_total, FRAME_HISTORY);
    for (uint32_t i = 0; i < stored; i++) {
        const Frame& f = frames[(frame_head + FRAME_HISTORY - i) % FRAME_HISTORY];
        if (f.number == p_number) return &f;
    }
    return nullptr;
}

const CpuProfiler::Frame* CpuProfiler::last_complete_frame() const
{
    if (frame_total < 2) return nullptr;
    return &frames[(frame_head + FRAME_HISTORY - 1) % FRAME_HISTORY];
}

const char* CpuProfiler::lane_name(uint32_t p_lane) const
{
    return (p_lane < lane_count.load(std::memory_order_acquire) && lanes[p_lane]) ? lanes[p_lane]->name : "?";
}

}