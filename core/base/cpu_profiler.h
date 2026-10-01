#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lumen {

struct CpuProfiler
{
    static constexpr uint32_t MAX_LANES = 64;
    static constexpr uint32_t MAX_DEPTH = 32;
    static constexpr uint32_t FRAME_HISTORY = 16;
    static constexpr uint8_t FLAG_WAIT = 1;

    struct Zone
    {
        const char* name;
        int64_t begin;
        int64_t end;
        uint16_t lane;
        uint8_t depth;
        uint8_t flags;
    };

    struct Lane
    {
        char name[32];
        uint16_t index;
        std::mutex mutex;
        std::vector<Zone> pending;

        const char* stack_name[MAX_DEPTH];
        int64_t stack_begin[MAX_DEPTH];
        uint8_t stack_flags[MAX_DEPTH];
        uint32_t depth;
    };

    struct Frame
    {
        uint64_t number;
        int64_t begin;
        int64_t end;
    };

    double ticks_to_ms = 0.0;
    double ticks_per_ns = 0.0;
    std::atomic<bool> enabled{ true };

    Lane* lanes[MAX_LANES] = {};
    std::atomic<uint32_t> lane_count{ 0 };
    std::mutex lane_mutex;
    uint16_t main_lane = 0;

    std::vector<Zone> zones;
    Frame frames[FRAME_HISTORY] = {};
    uint32_t frame_head = 0;
    uint64_t frame_total = 0;

    void initialize();
    void shutdown();

    void begin_frame(uint64_t p_number);
    void zone_begin(const char* p_name, uint8_t p_flags = 0);
    void zone_end();
    void set_thread_name(const char* p_name);

    const Frame* frame(uint64_t p_number) const;
    const Frame* last_complete_frame() const;
    const char* lane_name(uint32_t p_lane) const;

    Lane* _lane();
    static int64_t now();
};

CpuProfiler& cpu_profiler();

}