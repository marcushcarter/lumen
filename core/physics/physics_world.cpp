#include <core/physics/physics_world.h>
#include <core/physics/jolt_bridge.h>
#include <core/physics/collision_cache.h>
#include <core/rendering/debug_draw.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>
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
#include <cfloat>
#include <cmath>
#include <string>

namespace lumen {

using namespace jolt;

struct PhysicsWorld::Impl
{
    JPH::PhysicsSystem system;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::vector<JPH::Ref<JPH::CharacterVirtual>> characters;
    std::vector<JPH::Ref<JPH::Ragdoll>> ragdolls;
    uint32_t ragdoll_group = 0;

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
        default:
            return primitive_shape(p_shape);
    }
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
    settings->mInnerBodyShape = settings->mShape;
    settings->mInnerBodyLayer = to_jph(PhysicsLayer::CHARACTER);
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
    const JPH::RefConst<JPH::Shape> shape = _character_shape(p_radius, p_height);
    if (!character->SetShape(shape, 1.5f * character->GetCharacterPadding(), bp, ol, {}, {}, *impl->temp)) return false;
    character->SetInnerBodyShape(shape);
    return true;
}

void PhysicsWorld::set_character_max_slope(PhysicsCharacterId p_character, float p_degrees)
{
    if (JPH::CharacterVirtual* character = _character(impl.get(), p_character)) character->SetMaxSlopeAngle(JPH::DegreesToRadians(p_degrees));
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
    character->UpdateGroundVelocity();
    const JPH::Vec3 ground = character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround ? character->GetGroundVelocity() : JPH::Vec3::sZero();
    character->SetLinearVelocity(to_jph(p_move.velocity) + ground);
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(p_move.step_up, 0.0f), 0.0f);
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -std::max(p_move.stick_down, 0.0f), 0.0f);
    const QueryBroadPhaseFilter bp(PHYSICS_QUERY_WORLD);
    const QueryObjectFilter ol(PHYSICS_QUERY_WORLD);
    character->ExtendedUpdate(p_dt, impl->system.GetGravity(), settings, bp, ol, {}, {}, *impl->temp);
    r_result.position = to_glm(character->GetPosition());
    r_result.velocity = to_glm(character->GetLinearVelocity() - ground);
    r_result.ground_normal = to_glm(character->GetGroundNormal());
    const JPH::CharacterBase::EGroundState state = character->GetGroundState();
    r_result.on_ground = state == JPH::CharacterBase::EGroundState::OnGround;
    r_result.on_steep_ground = state == JPH::CharacterBase::EGroundState::OnSteepGround;
    return true;
}

PhysicsRagdollId PhysicsWorld::create_ragdoll(const PhysicsRagdollDesc& p_desc)
{
    if (!impl || p_desc.parts.empty()) return {};
    const uint32_t count = (uint32_t)p_desc.parts.size();
    JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton();
    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
    settings->mSkeleton = skeleton;
    settings->mParts.resize(count);
    std::vector<JPH::Mat44> matrices(count);
    for (uint32_t i = 0; i < count; i++) {
        const PhysicsRagdollPart& part = p_desc.parts[i];
        if (part.parent >= (int32_t)i) return {};
        skeleton->AddJoint("part" + std::to_string(i), part.parent);
        JPH::RefConst<JPH::Shape> shape = _make_shape(part.shape.shape, nullptr, PhysicsMotion::DYNAMIC);
        if (!shape) return {};
        shape = JPH::RotatedTranslatedShapeSettings(to_jph(part.shape.position), to_jph(normalize(part.shape.rotation)), shape).Create().Get();
        JPH::RagdollSettings::Part& p = settings->mParts[i];
        p.SetShape(shape);
        p.mPosition = to_jph_r(part.position);
        p.mRotation = to_jph(normalize(part.rotation));
        p.mMotionType = JPH::EMotionType::Dynamic;
        p.mMotionQuality = JPH::EMotionQuality::LinearCast;
        p.mObjectLayer = to_jph(PhysicsLayer::RAGDOLL);
        p.mFriction = p_desc.friction;
        p.mLinearDamping = p_desc.linear_damping;
        p.mAngularDamping = p_desc.angular_damping;
        p.mLinearVelocity = to_jph(part.linear_velocity);
        p.mAngularVelocity = to_jph(part.angular_velocity);
        p.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        p.mMassPropertiesOverride.mMass = std::max(part.mass, 0.1f);
        matrices[i] = JPH::Mat44::sRotationTranslation(p.mRotation, to_jph(part.position));
        if (part.parent < 0) continue;
        if (part.joint == PhysicsJoint::HINGE) {
            JPH::Ref<JPH::HingeConstraintSettings> c = new JPH::HingeConstraintSettings();
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPoint1 = c->mPoint2 = to_jph_r(part.joint_position);
            c->mHingeAxis1 = to_jph(normalize(part.parent_axis));
            c->mNormalAxis1 = to_jph(normalize(part.parent_normal));
            c->mHingeAxis2 = to_jph(normalize(part.child_axis));
            c->mNormalAxis2 = to_jph(normalize(part.child_normal));
            c->mLimitsMin = std::min(part.twist_min, 0.0f);
            c->mLimitsMax = std::max(part.twist_max, 0.0f);
            c->mMaxFrictionTorque = part.joint_friction;
            p.mToParent = c;
        } else {
            JPH::Ref<JPH::SwingTwistConstraintSettings> c = new JPH::SwingTwistConstraintSettings();
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPosition1 = c->mPosition2 = to_jph_r(part.joint_position);
            c->mTwistAxis1 = to_jph(normalize(part.parent_axis));
            c->mPlaneAxis1 = to_jph(normalize(part.parent_normal));
            c->mTwistAxis2 = to_jph(normalize(part.child_axis));
            c->mPlaneAxis2 = to_jph(normalize(part.child_normal));
            c->mNormalHalfConeAngle = part.swing_normal;
            c->mPlaneHalfConeAngle = part.swing_plane;
            c->mTwistMinAngle = part.twist_min;
            c->mTwistMaxAngle = part.twist_max;
            c->mMaxFrictionTorque = part.joint_friction;
            p.mToParent = c;
        }
    }
    skeleton->CalculateParentJointIndices();
    settings->Stabilize();
    settings->DisableParentChildCollisions(matrices.data(), p_desc.self_collision_gap);
    settings->CalculateBodyIndexToConstraintIndex();
    settings->CalculateConstraintIndexToBodyIdxPair();
    JPH::Ref<JPH::Ragdoll> ragdoll = settings->CreateRagdoll(++impl->ragdoll_group, p_desc.user_data, &impl->system);
    if (!ragdoll) {
        log_write("Physics: ragdoll creation failed.");
        return {};
    }
    ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
    for (uint32_t i = 0; i < (uint32_t)impl->ragdolls.size(); i++) {
        if (impl->ragdolls[i]) continue;
        impl->ragdolls[i] = ragdoll;
        return PhysicsRagdollId{ i };
    }
    impl->ragdolls.push_back(ragdoll);
    return PhysicsRagdollId{ (uint32_t)impl->ragdolls.size() - 1 };
}

void PhysicsWorld::destroy_ragdoll(PhysicsRagdollId p_ragdoll)
{
    if (!impl || !p_ragdoll.valid() || p_ragdoll.value >= impl->ragdolls.size() || !impl->ragdolls[p_ragdoll.value]) return;
    impl->ragdolls[p_ragdoll.value]->RemoveFromPhysicsSystem();
    impl->ragdolls[p_ragdoll.value] = nullptr;
}

void PhysicsWorld::destroy_all_ragdolls()
{
    if (!impl) return;
    for (JPH::Ref<JPH::Ragdoll>& ragdoll : impl->ragdolls) {
        if (ragdoll) ragdoll->RemoveFromPhysicsSystem();
    }
    impl->ragdolls.clear();
}

bool PhysicsWorld::read_ragdoll(PhysicsRagdollId p_ragdoll, std::vector<vec3>& r_positions, std::vector<quat>& r_rotations, std::vector<vec3>* r_velocities) const
{
    if (!impl || !p_ragdoll.valid() || p_ragdoll.value >= impl->ragdolls.size() || !impl->ragdolls[p_ragdoll.value]) return false;
    const JPH::Ragdoll& ragdoll = *impl->ragdolls[p_ragdoll.value];
    const uint32_t count = (uint32_t)ragdoll.GetBodyCount();
    r_positions.resize(count);
    r_rotations.resize(count);
    if (r_velocities) r_velocities->resize(count);
    const JPH::BodyInterface& bi = impl->bodies();
    for (uint32_t i = 0; i < count; i++) {
        const JPH::BodyID id = ragdoll.GetBodyID((int)i);
        JPH::RVec3 position;
        JPH::Quat rotation;
        bi.GetPositionAndRotation(id, position, rotation);
        r_positions[i] = to_glm(position);
        r_rotations[i] = to_glm(rotation);
        if (r_velocities) (*r_velocities)[i] = to_glm(bi.GetLinearVelocity(id));
    }
    return true;
}

void PhysicsWorld::drive_ragdoll(PhysicsRagdollId p_ragdoll, const quat* p_relative, const float* p_torque, uint32_t p_count, float p_frequency)
{
    if (!impl || !p_ragdoll.valid() || p_ragdoll.value >= impl->ragdolls.size() || !impl->ragdolls[p_ragdoll.value]) return;
    JPH::Ragdoll& ragdoll = *impl->ragdolls[p_ragdoll.value];
    const JPH::RagdollSettings* settings = ragdoll.GetRagdollSettings();
    const uint32_t count = std::min(p_count, (uint32_t)ragdoll.GetBodyCount());
    for (uint32_t i = 0; i < count; i++) {
        const int index = settings->GetConstraintIndexForBodyIndex((int)i);
        if (index < 0) continue;
        JPH::TwoBodyConstraint* constraint = ragdoll.GetConstraint(index);
        const float torque = std::max(p_torque[i], 0.0f);
        const JPH::EMotorState state = torque > 0.0f ? JPH::EMotorState::Position : JPH::EMotorState::Off;
        const JPH::MotorSettings motor(p_frequency, 1.0f, FLT_MAX, torque);
        if (constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist) {
            JPH::SwingTwistConstraint* st = static_cast<JPH::SwingTwistConstraint*>(constraint);
            st->GetSwingMotorSettings() = motor;
            st->GetTwistMotorSettings() = motor;
            st->SetSwingMotorState(state);
            st->SetTwistMotorState(state);
            if (torque > 0.0f) st->SetTargetOrientationBS(to_jph(normalize(p_relative[i])));
        } else if (constraint->GetSubType() == JPH::EConstraintSubType::Hinge) {
            JPH::HingeConstraint* h = static_cast<JPH::HingeConstraint*>(constraint);
            h->GetMotorSettings() = motor;
            h->SetMotorState(state);
            if (torque > 0.0f) h->SetTargetOrientationBS(to_jph(normalize(p_relative[i])));
        }
    }
    if (count > 0) impl->bodies().ActivateBody(ragdoll.GetBodyID(0));
}

void PhysicsWorld::shutdown()
{
    if (!impl) return;
    destroy_all_characters();
    destroy_all_ragdolls();
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
    destroy_all_ragdolls();
    destroy_all_characters();
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

float PhysicsWorld::body_mass(PhysicsBodyId p_body) const
{
    if (!impl || !p_body.valid()) return INFINITY;
    JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(), to_jph(p_body));
    if (!lock.Succeeded()) return INFINITY;
    const JPH::Body& body = lock.GetBody();
    if (!body.IsDynamic()) return INFINITY;
    const float inverse = body.GetMotionProperties()->GetInverseMass();
    return inverse > 0.0f ? 1.0f / inverse : INFINITY;
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
    _steps++;
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
