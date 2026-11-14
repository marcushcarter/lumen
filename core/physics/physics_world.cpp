#include <core/physics/physics_world.h>
#include <core/physics/jolt_bridge.h>
#include <core/physics/collision_cache.h>
#include <core/rendering/debug_draw.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>

namespace lumen {

using namespace jolt;

struct PhysicsWorld::Impl
{
    JPH::PhysicsSystem system;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::vector<JPH::Ref<JPH::CharacterVirtual>> characters;

    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }
};

static constexpr float MIN_SHAPE_SIZE = 1e-3f;
static constexpr float MIN_DIRECTION_LENGTH = 1e-6f;

static JPH::RefConst<JPH::Shape> _make_shape(const PhysicsShape& p_shape, const CollisionCache* p_cache, PhysicsMotion p_motion)
{
    switch (p_shape.type) {
        case PhysicsShapeType::MESH_COLLISION: {
            CollisionType type = CollisionType::MESH;
            const JPH::Shape* shape = p_cache ? jolt::collision_shape(*p_cache, p_shape.mesh, type) : nullptr;
            if (!shape) {
                log_write("Physics: collision for mesh %s isn't loaded.", p_shape.mesh.to_string().c_str());
                return nullptr;
            }
            if (type == CollisionType::MESH && p_motion != PhysicsMotion::STATIC) {
                log_write("Physics: triangle mesh collision can only be used on static bodies.");
                return nullptr;
            }
            return shape;
        }
        case PhysicsShapeType::BOX: {
            const vec3 half = max(p_shape.half_extents, vec3(MIN_SHAPE_SIZE));
            const float convex = std::min(JPH::cDefaultConvexRadius, std::min(half.x, std::min(half.y, half.z)) * 0.5f);
            JPH::BoxShapeSettings settings(to_jph(half), convex);
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError()) {
                log_write("Physics: box shape failed: %s", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
        case PhysicsShapeType::SPHERE:
            return new JPH::SphereShape(std::max(p_shape.radius, MIN_SHAPE_SIZE));
        case PhysicsShapeType::CAPSULE:
            return new JPH::CapsuleShape(std::max(p_shape.half_height, MIN_SHAPE_SIZE), std::max(p_shape.radius, MIN_SHAPE_SIZE));
    }
    return nullptr;
}

static JPH::RefConst<JPH::Shape> _make_body_shape(const PhysicsBodyDesc& p_desc, const CollisionCache* p_cache)
{
    if (p_desc.parts.empty()) return _make_shape(p_desc.shape, p_cache, p_desc.motion);
    const PhysicsShapePart& first = p_desc.parts[0];
    if (p_desc.parts.size() == 1 && first.position == vec3(0.0f) && first.rotation == quat(1.0f, 0.0f, 0.0f, 0.0f)) return _make_shape(first.shape, p_cache, p_desc.motion);

    JPH::StaticCompoundShapeSettings compound;
    uint32_t added = 0;
    for (const PhysicsShapePart& part : p_desc.parts) {
        JPH::RefConst<JPH::Shape> shape = _make_shape(part.shape, p_cache, p_desc.motion);
        if (!shape) continue;
        compound.AddShape(to_jph(part.position), to_jph(normalize(part.rotation)), shape);
        added++;
    }
    if (added == 0) return nullptr;
    JPH::ShapeSettings::ShapeResult result = compound.Create();
    if (result.HasError()) {
        log_write("Physics: compound shape failed: %s", result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

static JPH::EMotionType _motion(PhysicsMotion p_motion)
{
    switch (p_motion) {
        case PhysicsMotion::KINEMATIC: return JPH::EMotionType::Kinematic;
        case PhysicsMotion::DYNAMIC: return JPH::EMotionType::Dynamic;
        default: return JPH::EMotionType::Static;
    }
}

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
    shutdown();
}

Error PhysicsWorld::initialize(const PhysicsWorldSettings& p_settings)
{
    using enum Error;
    if (impl) return OK;
    LUMEN_ERR_FAIL_COND_V_MSG(!jolt::jobs(), FAILED, "PhysicsWorld: physics_initialize() was not called.");

    impl = std::make_unique<Impl>();
    impl->temp = std::make_unique<JPH::TempAllocatorImpl>(p_settings.temp_allocator_bytes);
    const Layers& l = layers();
    impl->system.Init(p_settings.max_bodies, 0, p_settings.max_body_pairs, p_settings.max_contact_constraints, l.broad_phase, *l.object_vs_broad_phase, l.pairs);
    impl->system.SetGravity(to_jph(p_settings.gravity));
    accumulator = 0.0f;
    return OK;
}

static JPH::RefConst<JPH::Shape> _character_shape(float p_radius, float p_height)
{
    const float radius = std::max(p_radius, MIN_SHAPE_SIZE);
    const float half = std::max(p_height * 0.5f - radius, MIN_SHAPE_SIZE);
    return JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, half + radius, 0.0f), JPH::Quat::sIdentity(), new JPH::CapsuleShape(half, radius)).Create().Get();
}

PhysicsCharacterId PhysicsWorld::create_character(const vec3& p_position, float p_radius, float p_height, float p_max_slope_degrees)
{
    if (!impl) return {};
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = _character_shape(p_radius, p_height);
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(p_max_slope_degrees);
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -std::max(p_radius, MIN_SHAPE_SIZE));
    settings->mUp = JPH::Vec3::sAxisY();
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(settings, to_jph_r(p_position), JPH::Quat::sIdentity(), 0, &impl->system);
    for (uint32_t i = 0; i < (uint32_t)impl->characters.size(); i++) {
        if (impl->characters[i]) continue;
        impl->characters[i] = character;
        return PhysicsCharacterId{ i };
    }
    impl->characters.push_back(character);
    return PhysicsCharacterId{ (uint32_t)impl->characters.size() - 1 };
}

void PhysicsWorld::destroy_character(PhysicsCharacterId p_character)
{
    if (!impl || !p_character.valid() || p_character.value >= impl->characters.size()) return;
    impl->characters[p_character.value] = nullptr;
}

void PhysicsWorld::destroy_all_characters()
{
    if (impl) impl->characters.clear();
}

static JPH::CharacterVirtual* _character(PhysicsWorld::Impl* p_impl, PhysicsCharacterId p_id)
{
    if (!p_impl || !p_id.valid() || p_id.value >= p_impl->characters.size()) return nullptr;
    return p_impl->characters[p_id.value].GetPtr();
}

bool PhysicsWorld::set_character_shape(PhysicsCharacterId p_character, float p_radius, float p_height)
{
    JPH::CharacterVirtual* character = _character(impl.get(), p_character);
    if (!character) return false;
    const QueryBroadPhaseFilter bp(PHYSICS_QUERY_WORLD);
    const QueryObjectFilter ol(PHYSICS_QUERY_WORLD);
    return character->SetShape(_character_shape(p_radius, p_height), 1.5f * character->GetCharacterPadding(), bp, ol, {}, {}, *impl->temp);
}

void PhysicsWorld::set_character_position(PhysicsCharacterId p_character, const vec3& p_position)
{
    if (JPH::CharacterVirtual* character = _character(impl.get(), p_character)) {
        character->SetPosition(to_jph_r(p_position));
        character->SetLinearVelocity(JPH::Vec3::sZero());
    }
}

bool PhysicsWorld::move_character(PhysicsCharacterId p_character, const PhysicsCharacterMove& p_move, float p_dt, PhysicsCharacterResult& r_result)
{
    JPH::CharacterVirtual* character = _character(impl.get(), p_character);
    if (!character || p_dt <= 0.0f) return false;
    character->SetLinearVelocity(to_jph(p_move.velocity));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(p_move.step_up, 0.0f), 0.0f);
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -std::max(p_move.stick_down, 0.0f), 0.0f);
    const QueryBroadPhaseFilter bp(PHYSICS_QUERY_WORLD);
    const QueryObjectFilter ol(PHYSICS_QUERY_WORLD);
    character->ExtendedUpdate(p_dt, impl->system.GetGravity(), settings, bp, ol, {}, {}, *impl->temp);
    r_result.position = to_glm(character->GetPosition());
    r_result.velocity = to_glm(character->GetLinearVelocity());
    r_result.ground_normal = to_glm(character->GetGroundNormal());
    const JPH::CharacterBase::EGroundState state = character->GetGroundState();
    r_result.on_ground = state == JPH::CharacterBase::EGroundState::OnGround;
    r_result.on_steep_ground = state == JPH::CharacterBase::EGroundState::OnSteepGround;
    return true;
}

void PhysicsWorld::shutdown()
{
    if (!impl) return;
    destroy_all_characters();
    destroy_all_bodies();
    impl.reset();
    accumulator = 0.0f;
}

PhysicsBodyId PhysicsWorld::create_body(const PhysicsBodyDesc& p_desc)
{
    if (!impl) return {};
    JPH::RefConst<JPH::Shape> shape = _make_body_shape(p_desc, collision_cache);
    if (!shape) return {};
    if (p_desc.scale != vec3(1.0f)) {
        JPH::Shape::ShapeResult scaled = shape->ScaleShape(to_jph(p_desc.scale));
        if (scaled.HasError()) log_write("Physics: couldn't scale shape: %s", scaled.GetError().c_str());
        else shape = scaled.Get();
    }

    const PhysicsLayer layer = p_desc.motion == PhysicsMotion::STATIC && p_desc.layer != PhysicsLayer::TRIGGER ? PhysicsLayer::STATIC : p_desc.layer;
    JPH::BodyCreationSettings settings(shape, to_jph_r(p_desc.position), to_jph(normalize(p_desc.rotation)), _motion(p_desc.motion), to_jph(layer));
    settings.mFriction = p_desc.friction;
    settings.mRestitution = p_desc.restitution;
    settings.mIsSensor = p_desc.sensor;
    settings.mUserData = p_desc.user_data;
    if (p_desc.mass > 0.0f) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = p_desc.mass;
    }

    const JPH::EActivation activation = p_desc.motion == PhysicsMotion::STATIC ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
    const JPH::BodyID id = impl->bodies().CreateAndAddBody(settings, activation);
    if (id.IsInvalid()) {
        log_write("Physics: body limit reached.");
        return {};
    }
    return to_lumen(id);
}

void PhysicsWorld::destroy_body(PhysicsBodyId p_body)
{
    if (!impl || !p_body.valid()) return;
    JPH::BodyInterface& bi = impl->bodies();
    const JPH::BodyID id = to_jph(p_body);
    if (!bi.IsAdded(id)) return;
    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

void PhysicsWorld::destroy_all_bodies()
{
    if (!impl) return;
    JPH::BodyIDVector ids;
    impl->system.GetBodies(ids);
    if (ids.empty()) return;
    JPH::BodyInterface& bi = impl->bodies();
    bi.RemoveBodies(ids.data(), (int)ids.size());
    bi.DestroyBodies(ids.data(), (int)ids.size());
}

uint32_t PhysicsWorld::body_count() const
{
    return impl ? impl->system.GetNumBodies() : 0;
}

void PhysicsWorld::optimize_broadphase()
{
    if (impl) impl->system.OptimizeBroadPhase();
}

void PhysicsWorld::set_transform(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation)
{
    if (!impl || !p_body.valid()) return;
    impl->bodies().SetPositionAndRotation(to_jph(p_body), to_jph_r(p_position), to_jph(normalize(p_rotation)), JPH::EActivation::Activate);
}

bool PhysicsWorld::get_transform(PhysicsBodyId p_body, vec3& r_position, quat& r_rotation) const
{
    if (!impl || !p_body.valid() || !impl->bodies().IsAdded(to_jph(p_body))) return false;
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl->bodies().GetPositionAndRotation(to_jph(p_body), position, rotation);
    r_position = to_glm(position);
    r_rotation = to_glm(rotation);
    return true;
}

void PhysicsWorld::move_kinematic(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation, float p_dt)
{
    if (!impl || !p_body.valid() || p_dt <= 0.0f) return;
    impl->bodies().MoveKinematic(to_jph(p_body), to_jph_r(p_position), to_jph(normalize(p_rotation)), p_dt);
}

void PhysicsWorld::set_linear_velocity(PhysicsBodyId p_body, const vec3& p_velocity)
{
    if (!impl || !p_body.valid()) return;
    impl->bodies().SetLinearVelocity(to_jph(p_body), to_jph(p_velocity));
}

vec3 PhysicsWorld::get_linear_velocity(PhysicsBodyId p_body) const
{
    if (!impl || !p_body.valid()) return vec3(0.0f);
    return to_glm(impl->bodies().GetLinearVelocity(to_jph(p_body)));
}

void PhysicsWorld::add_impulse(PhysicsBodyId p_body, const vec3& p_impulse)
{
    if (!impl || !p_body.valid()) return;
    impl->bodies().AddImpulse(to_jph(p_body), to_jph(p_impulse));
}

void PhysicsWorld::set_gravity(const vec3& p_gravity)
{
    if (impl) impl->system.SetGravity(to_jph(p_gravity));
}

vec3 PhysicsWorld::gravity() const
{
    return impl ? to_glm(impl->system.GetGravity()) : vec3(0.0f);
}

void PhysicsWorld::step_fixed()
{
    if (!impl) return;
    const JPH::EPhysicsUpdateError err = impl->system.Update(FIXED_DT, 1, impl->temp.get(), jolt::jobs());
    if (err != JPH::EPhysicsUpdateError::None) log_write("Physics: update reported error flags 0x%x.", (unsigned)err);
}

uint32_t PhysicsWorld::update(float p_dt)
{
    if (!impl || p_dt <= 0.0f) return 0;
    accumulator += p_dt;
    uint32_t steps = 0;
    while (accumulator >= FIXED_DT && steps < MAX_STEPS_PER_UPDATE) {
        step_fixed();
        accumulator -= FIXED_DT;
        steps++;
    }
    if (steps == MAX_STEPS_PER_UPDATE) accumulator = std::min(accumulator, FIXED_DT);
    return steps;
}

static bool _normalize_direction(const vec3& p_direction, vec3& r_out)
{
    const float len = length(p_direction);
    if (len < MIN_DIRECTION_LENGTH) return false;
    r_out = p_direction / len;
    return true;
}

static void _fill_hit(const JPH::PhysicsSystem& p_system, const JPH::BodyID& p_body, const JPH::SubShapeID& p_sub_shape, const vec3& p_position, const vec3* p_normal, float p_fraction, float p_max_distance, PhysicsHit& r_hit)
{
    r_hit.position = p_position;
    r_hit.fraction = p_fraction;
    r_hit.distance = p_fraction * p_max_distance;
    r_hit.body = to_lumen(p_body);
    JPH::BodyLockRead lock(p_system.GetBodyLockInterface(), p_body);
    if (!lock.Succeeded()) return;
    const JPH::Body& body = lock.GetBody();
    r_hit.user_data = body.GetUserData();
    r_hit.normal = p_normal ? *p_normal : to_glm(body.GetWorldSpaceSurfaceNormal(p_sub_shape, to_jph_r(p_position)));
}

bool PhysicsWorld::raycast(const vec3& p_origin, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query) const
{
    vec3 dir;
    if (!impl || p_max_distance <= 0.0f || !_normalize_direction(p_direction, dir)) return false;

    const JPH::RRayCast ray(to_jph_r(p_origin), to_jph(dir * p_max_distance));
    JPH::RayCastResult result;
    const QueryBroadPhaseFilter bp(p_query.layers);
    const QueryObjectFilter ol(p_query.layers);
    const JPH::IgnoreSingleBodyFilter body_filter(to_jph(p_query.ignore));
    if (!impl->system.GetNarrowPhaseQuery().CastRay(ray, result, bp, ol, body_filter)) return false;

    const vec3 point = p_origin + dir * (result.mFraction * p_max_distance);
    _fill_hit(impl->system, result.mBodyID, result.mSubShapeID2, point, nullptr, result.mFraction, p_max_distance, r_hit);
    return true;
}

static bool _shape_cast(const JPH::PhysicsSystem& p_system, const JPH::Shape* p_shape, const vec3& p_origin, const quat& p_rotation, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query)
{
    vec3 dir;
    if (p_max_distance <= 0.0f || !_normalize_direction(p_direction, dir)) return false;

    const JPH::RShapeCast cast(p_shape, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(to_jph(normalize(p_rotation)), to_jph_r(p_origin)), to_jph(dir * p_max_distance));
    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const QueryBroadPhaseFilter bp(p_query.layers);
    const QueryObjectFilter ol(p_query.layers);
    const JPH::IgnoreSingleBodyFilter body_filter(to_jph(p_query.ignore));
    p_system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, bp, ol, body_filter);
    if (!collector.HadHit()) return false;

    const JPH::ShapeCastResult& hit = collector.mHit;
    const vec3 axis = to_glm(hit.mPenetrationAxis);
    const float axis_len = length(axis);
    const vec3 normal = axis_len > MIN_DIRECTION_LENGTH ? -axis / axis_len : -dir;
    _fill_hit(p_system, hit.mBodyID2, hit.mSubShapeID2, to_glm(hit.mContactPointOn2), &normal, hit.mFraction, p_max_distance, r_hit);
    return true;
}

bool PhysicsWorld::sphere_cast(const vec3& p_origin, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query) const
{
    if (!impl || p_radius <= 0.0f) return false;
    const JPH::SphereShape shape(p_radius);
    shape.SetEmbedded();
    return _shape_cast(impl->system, &shape, p_origin, quat(1.0f, 0.0f, 0.0f, 0.0f), p_direction, p_max_distance, r_hit, p_query);
}

bool PhysicsWorld::capsule_cast(const vec3& p_origin, const quat& p_rotation, float p_half_height, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query) const
{
    if (!impl || p_radius <= 0.0f || p_half_height <= 0.0f) return false;
    const JPH::CapsuleShape shape(p_half_height, p_radius);
    shape.SetEmbedded();
    return _shape_cast(impl->system, &shape, p_origin, p_rotation, p_direction, p_max_distance, r_hit, p_query);
}

uint32_t PhysicsWorld::overlap_sphere(const vec3& p_center, float p_radius, std::vector<PhysicsBodyId>& r_bodies, const PhysicsQuery& p_query) const
{
    r_bodies.clear();
    if (!impl || p_radius <= 0.0f) return 0;

    const JPH::SphereShape shape(p_radius);
    shape.SetEmbedded();
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const QueryBroadPhaseFilter bp(p_query.layers);
    const QueryObjectFilter ol(p_query.layers);
    const JPH::IgnoreSingleBodyFilter body_filter(to_jph(p_query.ignore));
    impl->system.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(to_jph_r(p_center)), settings, JPH::RVec3::sZero(), collector, bp, ol, body_filter);

    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        const PhysicsBodyId id = to_lumen(hit.mBodyID2);
        if (std::find(r_bodies.begin(), r_bodies.end(), id) == r_bodies.end()) r_bodies.push_back(id);
    }
    return (uint32_t)r_bodies.size();
}

void PhysicsWorld::debug_draw(DebugDraw& r_draw, const vec3& p_camera_position) const
{
#ifdef JPH_DEBUG_RENDERER
    jolt::DebugRenderer* renderer = jolt::debug_renderer();
    if (!impl || !renderer) return;
    renderer->target = &r_draw;
    renderer->SetCameraPos(to_jph_r(p_camera_position));
    JPH::BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::MotionTypeColor;
    impl->system.DrawBodies(settings, renderer);
    renderer->NextFrame();
    renderer->target = nullptr;
#else
    (void)r_draw;
    (void)p_camera_position;
#endif
}

}
