#include <core/application/frame_limiter.h>
#include <algorithm>

namespace lumen {

Error FrameLimiter::initialize()
{
    using enum Error;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpc_freq = f.QuadPart;
    timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LUMEN_ERR_FAIL_COND_V_MSG(!timer, Failed, "FrameLimiter: high resolution waitable timer unavailable.");
    return Ok;
}

void FrameLimiter::shutdown()
{
    if (timer) CloseHandle(timer);
    timer = nullptr;
}

int64_t FrameLimiter::_now() const
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

void FrameLimiter::wait(float p_fps)
{
    if (p_fps <= 0.0f) return;
    if (p_fps < MIN_FPS) p_fps = MIN_FPS;
    const int64_t period = (int64_t)((double)qpc_freq / (double)p_fps);
    const int64_t spin = qpc_freq * SPIN_US / 1'000'000;
    int64_t now = _now();
    if (now < next) {
        const int64_t sleep_ticks = next - now - spin;
        if (sleep_ticks > 0) {
            LARGE_INTEGER due;
            due.QuadPart = -(sleep_ticks * 10'000'000 / qpc_freq);
            if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) WaitForSingleObject(timer, INFINITE);
        }
        while ((now = _now()) < next) YieldProcessor();
    }
    next = std::max(next + period, now);
}

}