#pragma once
#include <core/assets/guid.h>
#include <core/assets/lskeleton.h>
#include <core/physics/physics_types.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace lumen {

using namespace glm;

struct EntityIdComponent { Guid guid; };

struct NameComponent
{
    static constexpr uint32_t MAX_LENGTH = 64;
    char name[MAX_LENGTH] = {};
};

struct TransformComponent
{
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
};

struct MeshComponent
{
    Guid mesh;
    vec4 tint = vec4(1.0f);
};

struct PoseComponent
{
    std::vector<mat4> skin;
    std::vector<mat4> view_skin;
    std::vector<JointTransform> local;
    std::vector<mat4> joints;
    Guid skeleton;
    int32_t hidden_joint = -1;
};

enum class CollisionShapeType : uint8_t { BOX, SPHERE, CAPSULE, CYLINDER, TAPERED_CYLINDER, TAPERED_CAPSULE, WEDGE, COUNT };
enum class CollisionSource : uint8_t { MESH, SHAPES, COUNT };

static constexpr uint32_t COLLISION_MAX_SHAPES = 8;

struct CollisionShapeEntry
{
    CollisionShapeType type = CollisionShapeType::BOX;
    vec3 position = vec3(0.0f);
    vec3 rotation = vec3(0.0f);
    vec3 half_extents = vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
    float top_radius = 0.25f;
};

struct CollisionComponent
{
    CollisionSource source = CollisionSource::MESH;
    PhysicsMotion motion = PhysicsMotion::STATIC;
    float mass = 10.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool trigger = false;
    uint32_t shape_count = 0;
    CollisionShapeEntry shapes[COLLISION_MAX_SHAPES];
};

struct InitialVelocityComponent
{
    vec3 linear = vec3(0.0f);
    vec3 angular = vec3(0.0f);
};

enum class MoveGait : uint8_t { WALK, RUN, SPRINT, COUNT };

struct MovementComponent
{
    bool player = false;
    bool strafe = false;
    float radius = 0.3f;
    float height = 1.8f;
    float step_up = 0.45f;
    float max_slope = 50.0f;
    float walk_speed = 1.75f;
    float run_speed = 3.75f;
    float sprint_speed = 6.0f;
    float crouch_speed = 1.4f;
    float crouch_height = 1.2f;
    float acceleration = 10.0f;
    float deceleration = 12.0f;
    float turn_rate = 720.0f;
    float brake = 25.0f;
    float redirect_rate = 450.0f;
    float responsiveness = 0.5f;
    float jump_height = 1.1f;
    float air_control = 0.25f;

    vec3 want_move = vec3(0.0f);
    MoveGait want_gait = MoveGait::RUN;
    vec3 want_look = vec3(0.0f, 0.0f, 1.0f);

    vec3 velocity = vec3(0.0f);
    float facing = 0.0f;
    float target_facing = 0.0f;
    bool facing_ready = false;
    bool grounded = false;
    bool simulated = false;
    bool strafing = false;
    bool pivoting = false;
    bool want_crouch = false;
    bool crouching = false;
    bool foot_planted = true;
    vec3 root_motion = vec3(0.0f);
    bool want_roll = false;
    bool rolling = false;
    float roll_camera = 0.0f;
    float jump_buffer = 0.0f;
    float jump_lock = 0.0f;
    float air_time = 0.0f;
    float fall_speed = 0.0f;
    float land_speed = 0.0f;
    bool jumped = false;
    bool mantle_request = false;
    bool mantling = false;
    bool mantle_sync = false;
    vec3 mantle_start = vec3(0.0f);
    vec3 mantle_target = vec3(0.0f);
    float mantle_facing = 0.0f;
    float mantle_height = 0.0f;

    float gait_speed(MoveGait p_gait) const { return p_gait == MoveGait::WALK ? walk_speed : p_gait == MoveGait::SPRINT ? sprint_speed : run_speed; }
};

enum class CameraType : uint8_t { ORBIT, FIRST_PERSON, COUNT };

struct CameraComponent
{
    static constexpr uint32_t BONE_LENGTH = 64;

    CameraType type = CameraType::ORBIT;
    float distance = 3.0f;
    float shoulder = 0.35f;
    float pivot_height = 1.6f;
    float radius = 0.2f;
    float pitch_min = -70.0f;
    float pitch_max = 60.0f;
    char head_bone[BONE_LENGTH] = {};
    vec3 eye_offset = vec3(0.0f, 0.08f, 0.12f);
    float fp_pitch_min = -85.0f;
    float fp_pitch_max = 85.0f;
    bool hide_head = true;
    bool actions_third_person = true;

    bool viewing = false;
    float third_person = 0.0f;
};

struct LocomotionComponent
{
    Guid set;
};

struct LookAtComponent
{
    static constexpr float DEFAULT_YAW_MAX = 70.0f;

    float yaw_max = DEFAULT_YAW_MAX;
    float pitch_max = 60.0f;
    float speed = 10.0f;
    float camera_weight = 0.0f;
    vec3 split = vec3(0.3f, 0.3f, 0.4f);
};

struct AttentionPointComponent
{
    bool enabled = true;
    float priority = 0.5f;
    float radius = 10.0f;
    bool head = false;
    vec3 offset = vec3(0.0f);
};

enum class WaterSourceType : uint8_t { SPRING, LEVEL, COUNT };

struct WaterSourceComponent
{
    WaterSourceType type = WaterSourceType::SPRING;
    vec3 half_extents = vec3(1.0f);
    float flow = 1.0f;
};

struct ParentComponent
{
    Guid parent;
};

struct TemplateInstanceComponent
{
    Guid tmpl;
};

struct TemplatePartComponent
{
    Guid instance;
    Guid source;
};

struct StaticTag {};
struct DisabledTag {};
struct EditorExpandedTag {};

}
