#include <core/base/tasks.h>
#include <core/base/cpu_profiler.h>
#include <cstdio>

namespace lumen {

void TaskSystem::start(uint32_t p_worker_count, uint32_t p_high_reserve)
{
    high_reserve = p_high_reserve;
    if (running) return;
    running = true;
    workers.reserve(p_worker_count);
    for (uint32_t i = 0; i < p_worker_count; i++)
        workers.emplace_back([this, i]{
            char name[32];
            std::snprintf(name, sizeof(name), "Worker %u", i);
            cpu_profiler().set_thread_name(name);
            _worker_loop();
        });
}

void TaskSystem::stop()
{
    { std::lock_guard lock(mutex); running = false; }
    cv.notify_all();
    for (auto& t : workers) if (t.joinable()) t.join();
    workers.clear();
    high.clear();
    normal.clear();
    normal_in_flight = 0;
}

bool TaskSystem::_pop(Task& r_task)
{
    if (!high.empty()) {
        r_task = std::move(high.front());
        high.pop_front();
        return true;
    }
    if (!normal.empty() && normal_in_flight < _normal_cap()) {
        r_task = std::move(normal.front());
        normal.pop_front();
        normal_in_flight++; 
        return true;
    }
    return false;
}

bool TaskSystem::_try_run_one()
{
    Task task;
    { std::lock_guard lock(mutex); if (!_pop(task)) return false; }
    cpu_profiler().zone_begin("Task");
    task.fn();
    cpu_profiler().zone_end();
    task.counter->fetch_sub(1, std::memory_order_release);
    if (task.is_normal) {
        { std::lock_guard lock(mutex); if (normal_in_flight) normal_in_flight--; }
        cv.notify_one();
    }
    return true;
}

uint32_t TaskSystem::_normal_cap() const
{
    const uint32_t n = (uint32_t)workers.size();
    return n > high_reserve ? n - high_reserve : 1u;
}

void TaskSystem::_worker_loop()
{
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [this]{ return !high.empty() || (!normal.empty() && normal_in_flight < _normal_cap()) || !running; });
            if (!_pop(task)) { if (!running) return; continue; }
        }
        cpu_profiler().zone_begin("Task");
        task.fn();
        cpu_profiler().zone_end();
        task.counter->fetch_sub(1, std::memory_order_release);
        if (task.is_normal) {
            { std::lock_guard lock(mutex); if (normal_in_flight) normal_in_flight--; }
            cv.notify_one();
        }
    }
}

TaskSystem::Handle TaskSystem::dispatch(std::function<void()> fn, Priority p)
{
    Handle counter = std::make_shared<std::atomic<uint32_t>>(1);
    Task t{ std::move(fn), counter, p == Priority::NORMAL };
    { std::lock_guard lock(mutex); (p == Priority::HIGH ? high : normal).push_back(std::move(t)); }   // push t
    cv.notify_one();
    return counter;
}

void TaskSystem::parallel_for(uint32_t p_count, std::function<uint32_t(uint32_t)>& fn)
{
    if (p_count == 0) return;
    uint32_t chunks = workers.empty() ? 1u : (uint32_t)workers.size();
    if (chunks > p_count) chunks = p_count;
    uint32_t per = (p_count + chunks - 1) / chunks;

    Handle counter = std::make_shared<std::atomic<uint32_t>>(chunks);
    {
        std::lock_guard lock(mutex);
        for (uint32_t c = 0; c < chunks; c++) {
            uint32_t begin = c * per;
            uint32_t end = begin + per > p_count ? p_count : begin + per;
            high.push_back(Task{ [&fn, begin, end]{ for (uint32_t i = begin; i < end; i++) fn(i); }, counter });
        }
    }
    cv.notify_all();
    wait(counter);
}

void TaskSystem::wait(const Handle& handle)
{
    while (handle->load(std::memory_order_acquire) != 0)
        if (!_try_run_one()) std::this_thread::yield();
}

}