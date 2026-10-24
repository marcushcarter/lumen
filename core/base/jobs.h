#pragma once
#include <core/base/cpu_profiler.h>
#include <core/base/error.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumen {

struct SpinLock
{
    std::atomic<uint32_t> state{ 0 };

    void lock();
    bool try_lock() { return state.load(std::memory_order_relaxed) == 0 && state.exchange(1, std::memory_order_acquire) == 0; }
    void unlock() { state.store(0, std::memory_order_release); }
};

enum class JobPriority : uint8_t { HIGH, NORMAL, LOW, COUNT };

struct JobDecl
{
    void (*entry)(void* p_data) = nullptr;
    void* data = nullptr;
    const char* name = "Job";
    void (*discard)(void* p_data) = nullptr;
};

struct JobFiber;

struct JobWaitNode
{
    JobFiber* fiber;
    uint32_t target;
    JobWaitNode* next;
};

struct JobCounter
{
    std::atomic<uint32_t> value{ 0 };
    SpinLock lock;
    JobWaitNode* waiters = nullptr;

    bool done() {
        if (value.load(std::memory_order_acquire) != 0) return false;
        lock.lock();
        lock.unlock();
        return true;
    }
};

struct JobFiber
{
    void* handle = nullptr;
    uint32_t pinned_thread = UINT32_MAX;
    JobPriority priority = JobPriority::NORMAL;
    CpuProfiler::FiberZones zones{};
};

struct JobSystemCreateInfo
{
    uint32_t worker_count = 0;
    uint32_t background_count = 0;
    uint32_t fiber_stack_reserve = 1024 * 1024;
    uint32_t fiber_stack_commit = 64 * 1024;
    bool pin_threads = true;
};

struct JobSystem
{
    static constexpr uint32_t ANY_THREAD = UINT32_MAX;
    static constexpr uint32_t MAIN_THREAD = 0;
    static constexpr uint32_t PRIORITY_COUNT = (uint32_t)JobPriority::COUNT;
    static constexpr uint32_t SPIN_BEFORE_SLEEP = 512;

    template <typename T>
    struct Queue {
        static constexpr uint32_t INITIAL_CAPACITY = 256;

        SpinLock lock;
        std::vector<T> ring;
        uint32_t head = 0;
        uint32_t count = 0;
        std::atomic<uint32_t> approx_size{ 0 };

        bool empty() const { return approx_size.load(std::memory_order_seq_cst) == 0; }

        void push(const T* p_items, uint32_t p_count, uint32_t p_stride = 1) {
            lock.lock();
            if (count + p_count > ring.size()) _grow(count + p_count);
            const uint32_t mask = (uint32_t)ring.size() - 1;
            for (uint32_t i = 0; i < p_count; i++) ring[(head + count + i) & mask] = p_items[i * p_stride];
            count += p_count;
            approx_size.store(count, std::memory_order_seq_cst);
            lock.unlock();
        }

        bool pop(T& r_item) {
            if (empty()) return false;
            lock.lock();
            if (count == 0) {
                lock.unlock();
                return false;
            }
            r_item = ring[head];
            head = (head + 1) & ((uint32_t)ring.size() - 1);
            count--;
            approx_size.store(count, std::memory_order_seq_cst);
            lock.unlock();
            return true;
        }

        void _grow(uint32_t p_min) {
            uint32_t capacity = ring.empty() ? INITIAL_CAPACITY : (uint32_t)ring.size();
            while (capacity < p_min) capacity *= 2;
            std::vector<T> next(capacity);
            const uint32_t mask = ring.empty() ? 0 : (uint32_t)ring.size() - 1;
            for (uint32_t i = 0; i < count; i++) next[i] = ring[(head + i) & mask];
            ring = std::move(next);
            head = 0;
        }
    };

    enum class SwitchAction : uint8_t { NONE, FREE, WAIT };

    struct Job {
        JobDecl decl;
        JobCounter* counter = nullptr;
        JobPriority priority = JobPriority::NORMAL;
    };

    struct BackgroundJob {
        JobDecl decl;
        JobCounter* counter = nullptr;
    };

    struct Worker {
        uint32_t index = 0;
        std::thread thread;
        JobFiber home;
        JobFiber* current = nullptr;
        Queue<JobFiber*> pinned_ready;
        std::atomic<uint32_t> wake{ 0 };
        std::atomic<uint32_t> sleeping{ 0 };

        JobFiber* previous = nullptr;
        SwitchAction previous_action = SwitchAction::NONE;
        SpinLock* previous_lock = nullptr;
    };

    JobSystemCreateInfo info;
    std::atomic<bool> running{ false };
    std::vector<std::unique_ptr<Worker>> workers;
    Queue<Job> jobs[PRIORITY_COUNT];
    Queue<JobFiber*> ready[PRIORITY_COUNT];
    std::atomic<uint32_t> wake_cursor{ 0 };

    JobFiber main_fiber;
    bool main_converted = false;

    std::vector<std::thread> background;
    std::mutex background_mutex;
    std::condition_variable background_cv;
    std::deque<BackgroundJob> background_queue;
    bool background_running = false;

    SpinLock fiber_lock;
    std::vector<JobFiber*> fiber_free;
    std::vector<JobFiber*> fiber_all;

    void _discard_queued();
    Error initialize(const JobSystemCreateInfo& p_info = {});
    void shutdown();

    Worker* _worker() const;
    void _worker_thread(uint32_t p_index);
    void _background_thread(uint32_t p_index);
    void _pin_current_thread(uint32_t p_index);
    uint32_t thread_count() const { return (uint32_t)workers.size(); }
    uint32_t thread_index() const;
    bool on_main_thread() const { return thread_index() == MAIN_THREAD; }

    void _counter_add(JobCounter* r_counter, uint32_t p_count);
    void _counter_decrement(JobCounter* r_counter, uint32_t p_count);
    void _os_wait(JobCounter& r_counter, uint32_t p_target);
    void run(const JobDecl* p_jobs, uint32_t p_count, JobPriority p_priority = JobPriority::NORMAL, JobCounter* r_counter = nullptr);
    void run_background(const JobDecl& p_job, JobCounter* r_counter = nullptr);
    void run_repeat(const JobDecl& p_job, uint32_t p_count, JobPriority p_priority = JobPriority::NORMAL, JobCounter* r_counter = nullptr);
    void wait(JobCounter& r_counter, uint32_t p_target = 0);
    
    template <typename Fn>
    void run_owned(Fn&& p_fn, JobPriority p_priority = JobPriority::NORMAL, JobCounter* r_counter = nullptr, const char* p_name = "Job") {
        const JobDecl decl = _owned_decl(std::forward<Fn>(p_fn), p_name);
        run(&decl, 1, p_priority, r_counter);
    }
    
    template <typename Fn>
    void run_background_owned(Fn&& p_fn, JobCounter* r_counter = nullptr, const char* p_name = "Background") {
        run_background(_owned_decl(std::forward<Fn>(p_fn), p_name), r_counter);
    }

    bool _has_work(const Worker& p_worker) const;
    void _make_ready(JobFiber* p_fiber);
    bool _next_ready(Worker& r_worker, JobFiber*& r_fiber);
    bool _next_job(Worker& r_worker, Job& r_job);
    bool _accepts(const Worker& p_worker, uint32_t p_priority) const { return p_worker.index != MAIN_THREAD || p_priority != (uint32_t)JobPriority::LOW; }
    void _execute(Worker& r_worker, Job& r_job);
    void _idle(Worker& r_worker);
    void _wake(Worker& r_worker);
    bool _wake_one(uint32_t p_priority);
    void _wake_all();
    
    template <typename Fn>
    static JobDecl _owned_decl(Fn&& p_fn, const char* p_name) {
        using F = std::decay_t<Fn>;
        JobDecl decl;
        decl.entry = [](void* p_data) {
            F* f = static_cast<F*>(p_data);
            (*f)();
            delete f;
        };
        decl.discard = [](void* p_data) { delete static_cast<F*>(p_data); };
        decl.data = new F(std::forward<Fn>(p_fn));
        decl.name = p_name;
        return decl;
    }

    static void __stdcall _fiber_entry(void* p_param);
    JobFiber* _fiber_acquire();
    void _fiber_loop();
    void _fiber_release(JobFiber* p_fiber);
    void _switch(JobFiber* p_from, JobFiber* p_to, SwitchAction p_action, SpinLock* p_lock);
    void _after_switch();

    template <typename Fn>
    void parallel_for(uint32_t p_count, uint32_t p_batch, Fn&& p_fn, JobPriority p_priority = JobPriority::HIGH, const char* p_name = "Parallel For") {
        if (p_count == 0) return;
        using F = std::remove_reference_t<Fn>;
        struct Context {
            F* fn;
            uint32_t count;
            uint32_t batch;
            uint32_t batches;
            std::atomic<uint32_t> next{ 0 };
        };
        Context ctx;
        ctx.fn = &p_fn;
        ctx.count = p_count;
        ctx.batch = p_batch ? p_batch : 1;
        ctx.batches = (p_count + ctx.batch - 1) / ctx.batch;

        auto body = [](void* p_data) {
            Context& c = *static_cast<Context*>(p_data);
            for (;;) {
                const uint32_t b = c.next.fetch_add(1, std::memory_order_relaxed);
                if (b >= c.batches) return;
                const uint32_t begin = b * c.batch;
                const uint32_t end = c.count - begin < c.batch ? c.count : begin + c.batch;
                for (uint32_t i = begin; i < end; i++) (*c.fn)(i);
            }
        };

        const uint32_t threads = thread_count() ? thread_count() : 1;
        const uint32_t helpers = (ctx.batches < threads ? ctx.batches : threads) - 1;
        JobCounter counter;
        if (helpers) run_repeat(JobDecl{ .entry = body, .data = &ctx, .name = p_name }, helpers, p_priority, &counter);
        body(&ctx);
        wait(counter);
    }
};

JobSystem& job_system();

}