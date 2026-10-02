#pragma once
#include <core/base/error.h>
#include <windows.h>
#include <cstdint>

namespace lumen {

struct FrameLimiter
{
    static constexpr int64_t SPIN_US = 500;

    HANDLE timer = nullptr;
    int64_t qpc_freq = 0;
    int64_t next = 0;

    Error initialize();
    void shutdown();
    void wait(float p_fps);

    int64_t _now() const;
};

}