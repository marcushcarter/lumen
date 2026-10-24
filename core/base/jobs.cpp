#include <core/base/jobs.h>
#include <windows.h>
#include <immintrin.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace lumen {

void SpinLock::lock()
{
    for (;;) {
        if (state.exchange(1, std::memory_order_acquire) == 0) return;
        while (state.load(std::memory_order_relaxed) != 0) _mm_pause();
    }
}

/**************/
/**** JOBS ****/
/**************/

static thread_local JobSystem::Worker* t_worker = nullptr;

static void _thread_name(const char* p_name)
{
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const SetThreadDescriptionFn fn = (SetThreadDescriptionFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
    if (fn) {
        wchar_t wide[32];
        std::swprintf(wide, 32, L"%hs", p_name);
        fn(GetCurrentThread(), wide);
    }
    cpu_profiler().set_thread_name(p_name);
}

JobSystem& job_system()
{
    static JobSystem instance;
    return instance;
}

void JobSystem::_discard_queued()
{
    for (uint32_t p = 0; p < PRIORITY_COUNT; p++) {
        Job job;
        while (jobs[p].pop(job)) {
            if (job.decl.discard) job.decl.discard(job.decl.data);
            if (job.counter) _counter_decrement(job.counter, 1);
        }
    }
}

Error JobSystem::initialize(const JobSystemCreateInfo& p_info)
{
    using enum Error;
    if (running.load()) return OK;

    info = p_info;
    const uint32_t active = (uint32_t)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const uint32_t logical = active ? active : 1;
    const uint32_t worker_count = info.worker_count ? info.worker_count : logical;
    const uint32_t background_count = info.background_count ? info.background_count : (logical / 2 > 2 ? logical / 2 : 2);

    workers.clear();
    for (uint32_t i = 0; i < worker_count; i++) {
        workers.push_back(std::make_unique<Worker>());
        workers.back()->index = i;
    }

    Worker& main = *workers[MAIN_THREAD];
    main_fiber.handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    main_converted = main_fiber.handle != nullptr;
    if (!main_fiber.handle) main_fiber.handle = GetCurrentFiber();
    LUMEN_ERR_FAIL_COND_V_MSG(!main_fiber.handle || main_fiber.handle == (void*)(uintptr_t)0x1E00, FAILED, "JobSystem: could not convert the main thread to a fiber.");
    main_fiber.pinned_thread = MAIN_THREAD;
    main.current = &main_fiber;
    t_worker = &main;
    if (info.pin_threads) _pin_current_thread(MAIN_THREAD);
    _thread_name("Main");

    running.store(true);
    for (uint32_t i = 1; i < worker_count; i++) workers[i]->thread = std::thread(&JobSystem::_worker_thread, this, i);

    {
        std::lock_guard lock(background_mutex);
        background_running = true;
    }
    for (uint32_t i = 0; i < background_count; i++) background.emplace_back(&JobSystem::_background_thread, this, i);

    log_write("JobSystem: %u fiber workers (%s), %u background threads.", worker_count, info.pin_threads ? "pinned" : "unpinned", background_count);
    return OK;
}

void JobSystem::shutdown()
{
    if (!running.load()) return;
    if (_worker() != workers[MAIN_THREAD].get() || workers[MAIN_THREAD]->current != &main_fiber) {
        log_write("JobSystem: shutdown must be called from the main fiber.");
        return;
    }

    {
        std::lock_guard lock(background_mutex);
        background_running = false;
    }
    background_cv.notify_all();
    for (std::thread& t : background) {
        if (t.joinable()) t.join();
    }
    background.clear();
    for (BackgroundJob& j : background_queue) {
        if (j.decl.discard) j.decl.discard(j.decl.data);
        if (j.counter) _counter_decrement(j.counter, 1);
    }
    background_queue.clear();

    running.store(false);
    _wake_all();
    for (uint32_t i = 1; i < (uint32_t)workers.size(); i++) {
        if (workers[i]->thread.joinable()) workers[i]->thread.join();
    }
    _discard_queued();

    const uint32_t parked = (uint32_t)(fiber_all.size() - fiber_free.size());
    if (parked) log_write("JobSystem: %u fiber(s) were still waiting on counters at shutdown.", parked);
    for (JobFiber* f : fiber_all) {
        DeleteFiber(f->handle);
        delete f;
    }
    fiber_all.clear();
    fiber_free.clear();

    if (main_converted) ConvertFiberToThread();
    main_converted = false;
    main_fiber = {};
    t_worker = nullptr;
    workers.clear();
}

__declspec(noinline) JobSystem::Worker* JobSystem::_worker() const
{
    return t_worker;
}

void JobSystem::_worker_thread(uint32_t p_index)
{
    Worker& w = *workers[p_index];
    t_worker = &w;

    char name[32];
    std::snprintf(name, sizeof(name), "Job Worker %u", p_index);
    _thread_name(name);
    if (info.pin_threads) _pin_current_thread(p_index);

    w.home.handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!w.home.handle) {
        log_write("JobSystem: worker %u could not become a fiber (%lu).", p_index, GetLastError());
        t_worker = nullptr;
        return;
    }
    w.home.pinned_thread = p_index;
    w.current = &w.home;

    _switch(&w.home, _fiber_acquire(), SwitchAction::NONE, nullptr);

    ConvertFiberToThread();
    t_worker = nullptr;
}

void JobSystem::_background_thread(uint32_t p_index)
{
    char name[32];
    std::snprintf(name, sizeof(name), "Background %u", p_index);
    _thread_name(name);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    CpuProfiler& cpu = cpu_profiler();
    for (;;) {
        BackgroundJob job;
        {
            std::unique_lock lock(background_mutex);
            background_cv.wait(lock, [this] { return !background_queue.empty() || !background_running; });
            if (!background_running || background_queue.empty()) return;
            job = background_queue.front();
            background_queue.pop_front();
        }
        cpu.zone_begin(job.decl.name);
        job.decl.entry(job.decl.data);
        cpu.zone_end();
        if (job.counter) _counter_decrement(job.counter, 1);
    }
}

void JobSystem::_pin_current_thread(uint32_t p_index)
{
    if (p_index >= 64) return;
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << p_index);
}

uint32_t JobSystem::thread_index() const
{
    const Worker* w = _worker();
    return w ? w->index : ANY_THREAD;
}

void JobSystem::_counter_add(JobCounter* r_counter, uint32_t p_count)
{
    if (r_counter) r_counter->value.fetch_add(p_count, std::memory_order_acq_rel);
}

void JobSystem::_counter_decrement(JobCounter* r_counter, uint32_t p_count)
{
    JobFiber* wake[16];
    uint32_t wake_count = 0;
    std::vector<JobFiber*> overflow;

    r_counter->lock.lock();
    const uint32_t value = r_counter->value.fetch_sub(p_count, std::memory_order_acq_rel) - p_count;
    JobWaitNode** link = &r_counter->waiters;
    while (*link) {
        JobWaitNode* node = *link;
        if (node->target != value) {
            link = &node->next;
            continue;
        }
        *link = node->next;
        if (wake_count < 16) wake[wake_count++] = node->fiber;
        else overflow.push_back(node->fiber);
    }
    r_counter->lock.unlock();

    for (uint32_t i = 0; i < wake_count; i++) _make_ready(wake[i]);
    for (JobFiber* f : overflow) _make_ready(f);
}

void JobSystem::_os_wait(JobCounter& r_counter, uint32_t p_target)
{
    uint32_t spins = 0;
    while (r_counter.value.load(std::memory_order_acquire) != p_target) {
        if (++spins < 256) _mm_pause();
        else SwitchToThread();
    }
    r_counter.lock.lock();
    r_counter.lock.unlock();
}

void JobSystem::run(const JobDecl* p_jobs, uint32_t p_count, JobPriority p_priority, JobCounter* r_counter)
{
    if (p_count == 0) return;
    _counter_add(r_counter, p_count);

    const uint32_t p = (uint32_t)p_priority;
    constexpr uint32_t CHUNK = 64;
    Job chunk[CHUNK];
    for (uint32_t base = 0; base < p_count; base += CHUNK) {
        const uint32_t n = p_count - base < CHUNK ? p_count - base : CHUNK;
        for (uint32_t i = 0; i < n; i++) chunk[i] = { p_jobs[base + i], r_counter, p_priority };
        jobs[p].push(chunk, n);
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (uint32_t i = 0; i < p_count; i++) {
        if (!_wake_one(p)) break;
    }
}

void JobSystem::run_background(const JobDecl& p_job, JobCounter* r_counter)
{
    _counter_add(r_counter, 1);
    {
        std::lock_guard lock(background_mutex);
        if (!background_running) {
            if (p_job.discard) p_job.discard(p_job.data);
            if (r_counter) _counter_decrement(r_counter, 1);
            return;
        }
        background_queue.push_back({ p_job, r_counter });
    }
    background_cv.notify_one();
}

void JobSystem::run_repeat(const JobDecl& p_job, uint32_t p_count, JobPriority p_priority, JobCounter* r_counter)
{
    if (p_count == 0) return;
    _counter_add(r_counter, p_count);

    const uint32_t p = (uint32_t)p_priority;
    const Job job{ p_job, r_counter, p_priority };
    jobs[p].push(&job, p_count, 0);

    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (uint32_t i = 0; i < p_count; i++) {
        if (!_wake_one(p)) break;
    }
}

void JobSystem::wait(JobCounter& r_counter, uint32_t p_target)
{
    Worker* w = _worker();
    if (!w) {
        _os_wait(r_counter, p_target);
        return;
    }

    JobFiber* self = w->current;
    JobWaitNode node{ self, p_target, nullptr };

    r_counter.lock.lock();
    if (r_counter.value.load(std::memory_order_acquire) == p_target) {
        r_counter.lock.unlock();
        return;
    }
    node.next = r_counter.waiters;
    r_counter.waiters = &node;

    JobFiber* next = nullptr;
    if (!_next_ready(*w, next)) next = _fiber_acquire();
    _switch(self, next, SwitchAction::WAIT, &r_counter.lock);
}

bool JobSystem::_has_work(const Worker& p_worker) const
{
    if (!p_worker.pinned_ready.empty()) return true;
    for (uint32_t p = 0; p < PRIORITY_COUNT; p++) {
        if (!_accepts(p_worker, p)) continue;
        if (!ready[p].empty() || !jobs[p].empty()) return true;
    }
    return false;
}

void JobSystem::_make_ready(JobFiber* p_fiber)
{
    if (p_fiber->pinned_thread != ANY_THREAD) {
        Worker& w = *workers[p_fiber->pinned_thread];
        w.pinned_ready.push(&p_fiber, 1);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        _wake(w);
        return;
    }
    const uint32_t p = (uint32_t)p_fiber->priority;
    ready[p].push(&p_fiber, 1);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    _wake_one(p);
}

bool JobSystem::_next_ready(Worker& r_worker, JobFiber*& r_fiber)
{
    if (r_worker.pinned_ready.pop(r_fiber)) return true;
    for (uint32_t p = 0; p < PRIORITY_COUNT; p++) {
        if (_accepts(r_worker, p) && ready[p].pop(r_fiber)) return true;
    }
    return false;
}

bool JobSystem::_next_job(Worker& r_worker, Job& r_job)
{
    for (uint32_t p = 0; p < PRIORITY_COUNT; p++) {
        if (!_accepts(r_worker, p)) continue;
        if (!ready[p].empty()) return false;
        if (jobs[p].pop(r_job)) return true;
    }
    return false;
}

void JobSystem::_execute(Worker& r_worker, Job& r_job)
{
    r_worker.current->priority = r_job.priority;
    CpuProfiler& cpu = cpu_profiler();
    cpu.zone_begin(r_job.decl.name);
    r_job.decl.entry(r_job.decl.data);
    cpu.zone_end();
    if (r_job.counter) _counter_decrement(r_job.counter, 1);
}

void JobSystem::_idle(Worker& r_worker)
{
    for (uint32_t i = 0; i < SPIN_BEFORE_SLEEP; i++) {
        if (_has_work(r_worker) || !running.load(std::memory_order_relaxed)) return;
        _mm_pause();
    }

    const uint32_t seen = r_worker.wake.load();
    r_worker.sleeping.store(1);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (_has_work(r_worker) || !running.load()) {
        r_worker.sleeping.store(0);
        return;
    }

    CpuProfiler& cpu = cpu_profiler();
    cpu.zone_begin("Idle", CpuProfiler::FLAG_WAIT);
    r_worker.wake.wait(seen);
    cpu.zone_end();
    r_worker.sleeping.store(0);
}

void JobSystem::_wake(Worker& r_worker)
{
    uint32_t expected = 1;
    if (!r_worker.sleeping.compare_exchange_strong(expected, 0)) return;
    r_worker.wake.fetch_add(1);
    r_worker.wake.notify_one();
}

bool JobSystem::_wake_one(uint32_t p_priority)
{
    const uint32_t n = (uint32_t)workers.size();
    const uint32_t start = wake_cursor.fetch_add(1, std::memory_order_relaxed);
    for (uint32_t i = 0; i < n; i++) {
        Worker& w = *workers[(start + i) % n];
        if (!_accepts(w, p_priority)) continue;
        uint32_t expected = 1;
        if (!w.sleeping.compare_exchange_strong(expected, 0)) continue;
        w.wake.fetch_add(1);
        w.wake.notify_one();
        return true;
    }
    return false;
}

void JobSystem::_wake_all()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (std::unique_ptr<Worker>& w : workers) {
        w->sleeping.store(0);
        w->wake.fetch_add(1);
        w->wake.notify_one();
    }
}

void __stdcall JobSystem::_fiber_entry(void* p_param)
{
    JobSystem* js = static_cast<JobSystem*>(p_param);
    js->_after_switch();
    js->_fiber_loop();
}

JobFiber* JobSystem::_fiber_acquire()
{
    fiber_lock.lock();
    if (!fiber_free.empty()) {
        JobFiber* f = fiber_free.back();
        fiber_free.pop_back();
        fiber_lock.unlock();
        return f;
    }
    fiber_lock.unlock();

    JobFiber* f = new JobFiber();
    f->handle = CreateFiberEx(info.fiber_stack_commit, info.fiber_stack_reserve, FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)&JobSystem::_fiber_entry, this);
    if (!f->handle) {
        log_write("JobSystem: CreateFiberEx failed (%lu).", GetLastError());
        std::abort();
    }
    fiber_lock.lock();
    fiber_all.push_back(f);
    fiber_lock.unlock();
    return f;
}

void JobSystem::_fiber_loop()
{
    for (;;) {
        Worker* w = _worker();
        JobFiber* self = w->current;

        JobFiber* resumed = nullptr;
        if (_next_ready(*w, resumed)) {
            _switch(self, resumed, SwitchAction::FREE, nullptr);
            continue;
        }

        Job job;
        if (_next_job(*w, job)) {
            _execute(*w, job);
            continue;
        }

        if (!running.load() && w->index != MAIN_THREAD) {
            _switch(self, &w->home, SwitchAction::FREE, nullptr);
            continue;
        }

        _idle(*w);
    }
}

void JobSystem::_fiber_release(JobFiber* p_fiber)
{
    p_fiber->zones.depth = 0;
    p_fiber->priority = JobPriority::NORMAL;
    fiber_lock.lock();
    fiber_free.push_back(p_fiber);
    fiber_lock.unlock();
}

__declspec(noinline) void JobSystem::_switch(JobFiber* p_from, JobFiber* p_to, SwitchAction p_action, SpinLock* p_lock)
{
    Worker* w = _worker();
    cpu_profiler().fiber_suspend(p_from->zones);
    w->previous = p_from;
    w->previous_action = p_action;
    w->previous_lock = p_lock;
    w->current = p_to;
    SwitchToFiber(p_to->handle);
    _after_switch();
}

__declspec(noinline) void JobSystem::_after_switch()
{
    Worker* w = _worker();
    JobFiber* prev = w->previous;
    const SwitchAction action = w->previous_action;
    SpinLock* lock = w->previous_lock;
    w->previous = nullptr;
    w->previous_action = SwitchAction::NONE;
    w->previous_lock = nullptr;

    if (action == SwitchAction::FREE) _fiber_release(prev);
    else if (action == SwitchAction::WAIT) lock->unlock();

    cpu_profiler().fiber_resume(w->current->zones);
}

}