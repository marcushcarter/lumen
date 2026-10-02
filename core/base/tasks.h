#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lumen {

struct TaskSystem
{ 
    using Handle = std::shared_ptr<std::atomic<uint32_t>>;

    enum class Priority { HIGH = 0, NORMAL = 1 }; 

    struct Task {
        std::function<void()> fn;
        Handle counter;
        bool is_normal = false;
    };

    std::vector<std::thread> workers;
    std::deque<Task> high;
    std::deque<Task> normal;
    std::mutex mutex;
    std::condition_variable cv;
    bool running = false;

    uint32_t high_reserve = 1;
    uint32_t normal_in_flight = 0;

    void start(uint32_t p_worker_count, uint32_t p_high_reserve);
    void stop();

    bool _pop(Task& r_task);
    bool _try_run_one();
    void _worker_loop();
    uint32_t _normal_cap() const;

    Handle dispatch(std::function<void()> fn, Priority p = Priority::NORMAL);
    void parallel_for(uint32_t p_count, std::function<uint32_t(uint32_t)>& fn);
    void wait(const Handle& handle);
};

}