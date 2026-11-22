#pragma once
#include <core/physics/physics_types.h>
#include <core/physics/buoyancy.h>
#include <core/base/error.h>
#include <memory>
#include <vector>

namespace lumen {

struct DebugDraw;
struct CollisionCache;

struct PhysicsWorldSettings
{
    uint32_t max_bodies = 65536;
    uint32_t max_body_pairs = 65536;
    uint32_t max_contact_constraints = 16384;
    uint32_t temp_allocator_bytes = 16u * 1024u * 1024u;
    vec3 gravity = vec3(0.0f, -9.81f, 0.0f);
};

struct PhysicsHeightField
{
    void* shape = nullptr;
};

struct PhysicsWorld
{
    static constexpr float HEIGHT_FIELD_HOLE = 3.402823466e+38f;
    static constexpr uint64_t TERRAIN_USER_DATA = 0xFFFFFFFFFFFFFFFFull;

    static constexpr float FIXED_DT = 1.0f / 60.0f;
    static constexpr uint32_t MAX_STEPS_PER_UPDATE = 4;

    struct Impl;
    std::unique_ptr<Impl> impl;
    float accumulator = 0.0f;
    const CollisionCache* collision_cache = nullptr;
    uint64_t _steps = 0;

    static constexpr float WATER_DENSITY = 1000.0f;
    static constexpr uint32_t BUOYANCY_GRID = 4;
    static constexpr float BUOYANCY_SQUARE_PX = 7.0f;
    static constexpr float SPLASH_SPEED = 2.5f;
    static constexpr float SPLASH_MAX_SUBMERGED = 0.8f;
    static constexpr float SPLASH_COOLDOWN = 0.4f;
    static constexpr float WAKE_SUBMERGED = 0.05f;
    static inline NoWater NO_WATER;
    WaterQuery* water = &NO_WATER;
    bool buoyancy_forces = false;
    bool buoyancy_push = false;
    vec3 buoyancy_focus = vec3(0.0f);
    static constexpr size_t BUOYANCY_LOD_STRIDE = 8;
    static constexpr float BUOYANCY_LOD_VOLUME = 0.05f;
    static constexpr float BUOYANCY_LOD_DISTANCE = 30.0f;
    float buoyancy_drag = 1.0f;
    uint32_t buoyancy_grid = BUOYANCY_GRID;

    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    Error initialize(const PhysicsWorldSettings& p_settings = {});
    void shutdown();
    bool initialized() const { return impl != nullptr; }

    PhysicsBodyId create_body(const PhysicsBodyDesc& p_desc);
    static PhysicsHeightField build_height_field(const float* p_samples, uint32_t p_count, const vec3& p_offset, const vec3& p_scale);
    static void release_height_field(PhysicsHeightField& r_field);
    PhysicsBodyId create_height_field_body(PhysicsHeightField& r_field, float p_friction);
    void destroy_body(PhysicsBodyId p_body);
    void destroy_all_bodies();
    uint32_t body_count() const;
    void optimize_broadphase();

    void set_transform(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation);
    bool get_transform(PhysicsBodyId p_body, vec3& r_position, quat& r_rotation) const;
    bool body_bounds(PhysicsBodyId p_body, vec3& r_min, vec3& r_max) const;
    void move_kinematic(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation, float p_dt);
    void set_frozen(PhysicsBodyId p_body, bool p_frozen);
    void set_linear_velocity(PhysicsBodyId p_body, const vec3& p_velocity);
    void set_angular_velocity(PhysicsBodyId p_body, const vec3& p_velocity);
    vec3 get_linear_velocity(PhysicsBodyId p_body) const;
    float body_mass(PhysicsBodyId p_body) const;
    void add_impulse(PhysicsBodyId p_body, const vec3& p_impulse);
    void add_impulse_at(PhysicsBodyId p_body, const vec3& p_impulse, const vec3& p_point);
    vec3 point_velocity(PhysicsBodyId p_body, const vec3& p_point) const;
    void set_gravity(const vec3& p_gravity);
    vec3 gravity() const;

    uint32_t advance(float p_dt);
    uint32_t update(float p_dt);
    void step_fixed();
    float interpolation_alpha() const { return accumulator / FIXED_DT; }

    bool raycast(const vec3& p_origin, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    bool sphere_cast(const vec3& p_origin, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    bool capsule_cast(const vec3& p_origin, const quat& p_rotation, float p_half_height, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    bool overlaps_static_object(const vec3& p_min, const vec3& p_max) const;
    uint32_t overlap_sphere(const vec3& p_center, float p_radius, std::vector<PhysicsBodyId>& r_bodies, const PhysicsQuery& p_query = {}) const;


    PhysicsCharacterId create_character(const vec3& p_position, float p_radius, float p_height, float p_max_slope_degrees);
    void destroy_character(PhysicsCharacterId p_character);
    void destroy_all_characters();
    bool set_character_shape(PhysicsCharacterId p_character, float p_radius, float p_height);
    void set_character_position(PhysicsCharacterId p_character, const vec3& p_position);
    void set_character_max_slope(PhysicsCharacterId p_character, float p_degrees);
    bool move_character(PhysicsCharacterId p_character, const PhysicsCharacterMove& p_move, float p_dt, PhysicsCharacterResult& r_result);

    PhysicsRagdollId create_ragdoll(const PhysicsRagdollDesc& p_desc);
    void destroy_ragdoll(PhysicsRagdollId p_ragdoll);
    void destroy_all_ragdolls();
    bool read_ragdoll(PhysicsRagdollId p_ragdoll, std::vector<vec3>& r_positions, std::vector<quat>& r_rotations, std::vector<vec3>* r_velocities = nullptr) const;
    void drive_ragdoll(PhysicsRagdollId p_ragdoll, const quat* p_relative, const float* p_torque, uint32_t p_count, float p_frequency);
    uint64_t steps() const { return _steps; }

    void debug_draw(DebugDraw& r_draw, const vec3& p_camera_position) const;

    const BuoyancyBody* buoyancy(PhysicsBodyId p_body) const;
    void update_buoyancy() const;
    void _apply_buoyancy();
    void debug_draw_buoyancy(DebugDraw& r_draw, const vec3& p_center, float p_radius) const;
};

Error physics_initialize();
void physics_shutdown();

}
