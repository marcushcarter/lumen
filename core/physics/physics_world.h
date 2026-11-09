#pragma once
#include <core/physics/physics_types.h>
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

struct PhysicsWorld
{
    static constexpr float FIXED_DT = 1.0f / 60.0f;
    static constexpr uint32_t MAX_STEPS_PER_UPDATE = 4;

    struct Impl;
    std::unique_ptr<Impl> impl;
    float accumulator = 0.0f;
    const CollisionCache* collision_cache = nullptr;

    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    Error initialize(const PhysicsWorldSettings& p_settings = {});
    void shutdown();
    bool initialized() const { return impl != nullptr; }

    PhysicsBodyId create_body(const PhysicsBodyDesc& p_desc);
    void destroy_body(PhysicsBodyId p_body);
    void destroy_all_bodies();
    uint32_t body_count() const;
    void optimize_broadphase();

    void set_transform(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation);
    bool get_transform(PhysicsBodyId p_body, vec3& r_position, quat& r_rotation) const;
    void move_kinematic(PhysicsBodyId p_body, const vec3& p_position, const quat& p_rotation, float p_dt);
    void set_linear_velocity(PhysicsBodyId p_body, const vec3& p_velocity);
    vec3 get_linear_velocity(PhysicsBodyId p_body) const;
    void add_impulse(PhysicsBodyId p_body, const vec3& p_impulse);
    void set_gravity(const vec3& p_gravity);
    vec3 gravity() const;

    uint32_t update(float p_dt);
    void step_fixed();
    float interpolation_alpha() const { return accumulator / FIXED_DT; }

    bool raycast(const vec3& p_origin, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    bool sphere_cast(const vec3& p_origin, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    bool capsule_cast(const vec3& p_origin, const quat& p_rotation, float p_half_height, float p_radius, const vec3& p_direction, float p_max_distance, PhysicsHit& r_hit, const PhysicsQuery& p_query = {}) const;
    uint32_t overlap_sphere(const vec3& p_center, float p_radius, std::vector<PhysicsBodyId>& r_bodies, const PhysicsQuery& p_query = {}) const;

    void debug_draw(DebugDraw& r_draw, const vec3& p_camera_position) const;
};

Error physics_initialize();
void physics_shutdown();

}
