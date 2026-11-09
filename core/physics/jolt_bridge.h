#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif
#include <core/physics/physics_types.h>
#include <core/assets/guid.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <memory>
#include <string_view>

namespace lumen {

struct DebugDraw;
struct CollisionCache;
enum class CollisionType : uint8_t;

namespace jolt {

enum class BroadPhase : uint8_t { NON_MOVING, MOVING, TRIGGER, COUNT };

inline JPH::Vec3 to_jph(const vec3& p_v) { return JPH::Vec3(p_v.x, p_v.y, p_v.z); }
inline JPH::RVec3 to_jph_r(const vec3& p_v) { return JPH::RVec3(p_v.x, p_v.y, p_v.z); }
inline JPH::Quat to_jph(const quat& p_q) { return JPH::Quat(p_q.x, p_q.y, p_q.z, p_q.w); }
inline vec3 to_glm(JPH::Vec3Arg p_v) { return vec3(p_v.GetX(), p_v.GetY(), p_v.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
inline vec3 to_glm(JPH::RVec3Arg p_v) { return vec3((float)p_v.GetX(), (float)p_v.GetY(), (float)p_v.GetZ()); }
#endif
inline quat to_glm(JPH::QuatArg p_q) { return quat(p_q.GetW(), p_q.GetX(), p_q.GetY(), p_q.GetZ()); }

inline JPH::BodyID to_jph(PhysicsBodyId p_id) { return JPH::BodyID(p_id.value); }
inline PhysicsBodyId to_lumen(const JPH::BodyID& p_id) { return PhysicsBodyId{ p_id.GetIndexAndSequenceNumber() }; }
inline JPH::ObjectLayer to_jph(PhysicsLayer p_layer) { return (JPH::ObjectLayer)p_layer; }

BroadPhase broad_phase_of(PhysicsLayer p_layer);

struct Layers
{
    JPH::ObjectLayerPairFilterTable pairs{ (JPH::uint)PhysicsLayer::COUNT };
    JPH::BroadPhaseLayerInterfaceTable broad_phase{ (JPH::uint)PhysicsLayer::COUNT, (JPH::uint)BroadPhase::COUNT };
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> object_vs_broad_phase;

    void setup();
};

struct QueryObjectFilter final : JPH::ObjectLayerFilter
{
    uint32_t mask = 0;
    explicit QueryObjectFilter(uint32_t p_mask) : mask(p_mask) {}
    bool ShouldCollide(JPH::ObjectLayer p_layer) const override { return p_layer < 32 && (mask & (1u << p_layer)) != 0; }
};

struct QueryBroadPhaseFilter final : JPH::BroadPhaseLayerFilter
{
    uint32_t mask = 0;
    explicit QueryBroadPhaseFilter(uint32_t p_layers);
    bool ShouldCollide(JPH::BroadPhaseLayer p_layer) const override { return (mask & (1u << p_layer.GetValue())) != 0; }
};

class JobAdapter final : public JPH::JobSystemWithBarrier
{
public:
    static constexpr JPH::uint MAX_JOBS = 2048;
    static constexpr JPH::uint MAX_BARRIERS = 64;

    void init();

    int GetMaxConcurrency() const override;
    JPH::JobHandle CreateJob(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::uint32 p_dependencies = 0) override;
    void CreateJobs(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::JobHandle* r_jobs, JPH::uint32 p_count, JPH::uint32 p_dependencies = 0) override;

protected:
    void QueueJob(Job* p_job) override;
    void QueueJobs(Job** p_jobs, JPH::uint p_count) override;
    void FreeJob(Job* p_job) override;

private:
    using AvailableJobs = JPH::FixedSizeFreeList<Job>;
    AvailableJobs _jobs;

    Job* _allocate(const char* p_name, JPH::ColorArg p_color, const JobFunction& p_function, JPH::uint32 p_dependencies);
    static void _execute(void* p_job);
};

#ifdef JPH_DEBUG_RENDERER
class DebugRenderer final : public JPH::DebugRendererSimple
{
public:
    DebugDraw* target = nullptr;

    void DrawLine(JPH::RVec3Arg p_from, JPH::RVec3Arg p_to, JPH::ColorArg p_color) override;
    void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override {}
};

DebugRenderer* debug_renderer();
#endif

const Layers& layers();
const JPH::Shape* collision_shape(const CollisionCache& p_cache, Guid p_guid, CollisionType& r_type);
JPH::JobSystem* jobs();

}

}
