#include <core/physics/jolt_bridge.h>
#include <core/physics/physics_world.h>
#include <core/rendering/debug_draw.h>
#include <core/base/jobs.h>
#include <core/base/error.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace lumen {

namespace jolt {

static std::unique_ptr<Layers> g_layers;
static std::unique_ptr<JobAdapter> g_jobs;
#ifdef JPH_DEBUG_RENDERER
static std::unique_ptr<DebugRenderer> g_debug_renderer;
#endif

BroadPhase broad_phase_of(PhysicsLayer p_layer)
{
    switch (p_layer) {
        case PhysicsLayer::STATIC: return BroadPhase::NON_MOVING;
        case PhysicsLayer::TRIGGER: return BroadPhase::TRIGGER;
        default: return BroadPhase::MOVING;
    }
}

void Layers::setup()
{
    using enum PhysicsLayer;
    const auto enable = [this](PhysicsLayer p_a, PhysicsLayer p_b) { pairs.EnableCollision(to_jph(p_a), to_jph(p_b)); };
    enable(STATIC, DYNAMIC);
    enable(STATIC, KINEMATIC);
    enable(STATIC, CHARACTER);
    enable(DYNAMIC, DYNAMIC);
    enable(DYNAMIC, KINEMATIC);
    enable(DYNAMIC, CHARACTER);
    enable(DYNAMIC, TRIGGER);
    enable(KINEMATIC, CHARACTER);
    enable(KINEMATIC, TRIGGER);
    enable(CHARACTER, CHARACTER);
    enable(CHARACTER, TRIGGER);

    for (uint32_t l = 0; l < (uint32_t)PhysicsLayer::COUNT; l++) {
        broad_phase.MapObjectToBroadPhaseLayer((JPH::ObjectLayer)l, JPH::BroadPhaseLayer((JPH::BroadPhaseLayer::Type)broad_phase_of((PhysicsLayer)l)));
    }

    object_vs_broad_phase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, (JPH::uint)BroadPhase::COUNT, pairs, (JPH::uint)PhysicsLayer::COUNT);
}

QueryBroadPhaseFilter::QueryBroadPhaseFilter(uint32_t p_layers)
{
    for (uint32_t l = 0; l < (uint32_t)PhysicsLayer::COUNT; l++) {
        if (p_layers & (1u << l)) mask |= 1u << (uint32_t)broad_phase_of((PhysicsLayer)l);
    }
}

void JobAdapter::init()
{
    JobSystemWithBarrier::Init(MAX_BARRIERS);
    _jobs.Init(MAX_JOBS, MAX_JOBS);
}

int JobAdapter::GetMaxConcurrency() const
{
    return std::max(1, (int)lumen::job_system().thread_count());
}

JobAdapter::Job* JobAdapter::_allocate(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::uint32 p_dependencies)
{
    for (;;) {
        const JPH::uint32 index = _jobs.ConstructObject(p_name, p_color, this, p_function, p_dependencies);
        if (index != AvailableJobs::cInvalidObjectIndex) return &_jobs.Get(index);
        JPH_ASSERT(false, "Jolt job pool exhausted");
        std::this_thread::yield();
    }
}

JPH::JobHandle JobAdapter::CreateJob(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::uint32 p_dependencies)
{
    Job* job = _allocate(p_name, p_color, p_function, p_dependencies);
    JPH::JobHandle handle(job);
    if (p_dependencies == 0) QueueJob(job);
    return handle;
}

void JobAdapter::CreateJobs(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::JobHandle* r_jobs, JPH::uint32 p_count, JPH::uint32 p_dependencies)
{
    for (JPH::uint32 i = 0; i < p_count; i++) r_jobs[i] = JPH::JobHandle(_allocate(p_name, p_color, p_function, p_dependencies));
    if (p_dependencies != 0) return;
    static_assert(sizeof(JPH::JobHandle) == sizeof(Job*));
    QueueJobs(reinterpret_cast<Job**>(r_jobs), p_count);
}

void JobAdapter::_execute(void* p_job)
{
    Job* job = static_cast<Job*>(p_job);
    job->Execute();
    job->Release();
}

void JobAdapter::QueueJob(Job* p_job)
{
    p_job->AddRef();
    const JobDecl decl{ .entry = &JobAdapter::_execute, .data = p_job, .name = "Physics" };
    lumen::job_system().run(&decl, 1, JobPriority::HIGH);
}

void JobAdapter::QueueJobs(Job** p_jobs, JPH::uint p_count)
{
    static constexpr JPH::uint BATCH = 64;
    JobDecl decls[BATCH];
    JPH::uint done = 0;
    while (done < p_count) {
        const JPH::uint n = std::min(BATCH, p_count - done);
        for (JPH::uint i = 0; i < n; i++) {
            Job* job = p_jobs[done + i];
            job->AddRef();
            decls[i] = JobDecl{ .entry = &JobAdapter::_execute, .data = job, .name = "Physics" };
        }
        lumen::job_system().run(decls, n, JobPriority::HIGH);
        done += n;
    }
}

void JobAdapter::FreeJob(Job* p_job)
{
    _jobs.DestructObject(p_job);
}

#ifdef JPH_DEBUG_RENDERER
void DebugRenderer::DrawLine(JPH::RVec3Arg p_from, JPH::RVec3Arg p_to, JPH::ColorArg p_color)
{
    if (!target) return;
    target->line(to_glm(p_from), to_glm(p_to), { .color = p_color.GetUInt32(), .category = DebugCategory::COLLISION });
}

DebugRenderer* debug_renderer() { return g_debug_renderer.get(); }
#endif

const Layers& layers() { return *g_layers; }
JPH::JobSystem* jobs() { return g_jobs.get(); }

static void _trace(const char* p_fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, p_fmt);
    vsnprintf(buffer, sizeof(buffer), p_fmt, args);
    va_end(args);
    log_write("Physics: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool _assert_failed(const char* p_expression, const char* p_message, const char* p_file, JPH::uint p_line)
{
    log_write("Physics: assert '%s' failed at %s:%u%s%s", p_expression, p_file, p_line, p_message ? ": " : "", p_message ? p_message : "");
    return true;
}
#endif

}

Error physics_initialize()
{
    if (jolt::g_layers) return Error::OK;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = jolt::_trace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = jolt::_assert_failed;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    jolt::g_layers = std::make_unique<jolt::Layers>();
    jolt::g_layers->setup();
    jolt::g_jobs = std::make_unique<jolt::JobAdapter>();
    jolt::g_jobs->init();
#ifdef JPH_DEBUG_RENDERER
    jolt::g_debug_renderer = std::make_unique<jolt::DebugRenderer>();
#endif
    return Error::OK;
}

void physics_shutdown()
{
    if (!jolt::g_layers) return;
#ifdef JPH_DEBUG_RENDERER
    jolt::g_debug_renderer.reset();
#endif
    jolt::g_jobs.reset();
    jolt::g_layers.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

}
