#include <core/world/component_serialize.h>
#include <string>
#include <algorithm>

namespace lumen {

void serialize(Archive& a, NameComponent& r_c)
{
    a.field("name", r_c.name);
}

void serialize(Archive& a, TransformComponent& r_c)
{
    a.field("position", r_c.position);
    a.field("rotation", r_c.rotation);
    a.field("scale", r_c.scale);
}

void serialize(Archive& a, ModelComponent& r_c)
{
    a.field("model", r_c.model);
}

void serialize(Archive& a, AnimationComponent& r_c)
{
    a.field("blend_space", r_c.blend_space);
}

const char* camera_state_key(CameraState p_state)
{
    static const char* KEYS[CAMERA_STATE_COUNT] = { "explore", "sprint", "block", "aim", "lock_on", "mounted" };
    return (uint32_t)p_state < CAMERA_STATE_COUNT ? KEYS[(uint32_t)p_state] : "explore";
}

void serialize(Archive& a, PlayerSettings& r_c)
{
    a.field("controlled", r_c.controlled);
    CameraSettings& c = r_c.camera;
    a.field("pivot_height", c.pivot_height);
    a.field("pivot_lag", c.pivot_lag);
    a.field("pivot_lag_vertical", c.pivot_lag_vertical);
    a.field("pitch_min", c.pitch_min);
    a.field("pitch_max", c.pitch_max);
    a.field("start_pitch", c.start_pitch);
    a.field("mouse_sensitivity", c.mouse_sensitivity);
    a.field("stick_sensitivity", c.stick_sensitivity);
    a.field("invert_y", c.invert_y);
    a.field("distance_scale", c.distance_scale);
    a.field("probe_radius", c.probe_radius);
    a.field("return_time", c.return_time);
    for (uint32_t i = 0; i < CAMERA_STATE_COUNT; i++) {
        const std::string prefix = camera_state_key((CameraState)i);
        CameraStateSettings& s = c.states[i];
        a.field((prefix + "_distance").c_str(), s.distance);
        a.field((prefix + "_offset").c_str(), s.offset);
        a.field((prefix + "_fov").c_str(), s.fov);
        a.field((prefix + "_enter_time").c_str(), s.enter_time);
        a.field((prefix + "_exit_time").c_str(), s.exit_time);
    }
}

void serialize(Archive& a, CollisionComponent& r_c)
{
    uint32_t motion = (uint32_t)r_c.motion;
    a.field("motion", motion);
    r_c.motion = motion <= (uint32_t)PhysicsMotion::DYNAMIC ? (PhysicsMotion)motion : PhysicsMotion::STATIC;
    a.field("mass", r_c.mass);
    a.field("friction", r_c.friction);
    a.field("restitution", r_c.restitution);
    a.field("trigger", r_c.trigger);
    a.field("shape_count", r_c.shape_count);
    r_c.shape_count = std::min(r_c.shape_count, COLLISION_MAX_SHAPES);
    for (uint32_t i = 0; i < r_c.shape_count; i++) {
        const std::string p = "shape" + std::to_string(i) + "_";
        CollisionShapeEntry& s = r_c.shapes[i];
        uint32_t type = (uint32_t)s.type;
        a.field((p + "type").c_str(), type);
        s.type = type < (uint32_t)CollisionShapeType::COUNT ? (CollisionShapeType)type : CollisionShapeType::BOX;
        a.field((p + "position").c_str(), s.position);
        a.field((p + "rotation").c_str(), s.rotation);
        a.field((p + "half_extents").c_str(), s.half_extents);
        a.field((p + "radius").c_str(), s.radius);
        a.field((p + "half_height").c_str(), s.half_height);
    }
}

void serialize(Archive& a, CharacterComponent& r_c)
{
    a.field("radius", r_c.radius);
    a.field("height", r_c.height);
    a.field("walk_speed", r_c.walk_speed);
    a.field("jog_speed", r_c.jog_speed);
    a.field("sprint_speed", r_c.sprint_speed);
    a.field("acceleration", r_c.acceleration);
    a.field("deceleration", r_c.deceleration);
    a.field("air_control", r_c.air_control);
    a.field("turn_speed", r_c.turn_speed);
    a.field("jump_speed", r_c.jump_speed);
    a.field("gravity_scale", r_c.gravity_scale);
    a.field("max_slope", r_c.max_slope);
    a.field("step_height", r_c.step_height);
    a.field("yaw_offset", r_c.yaw_offset);
}

void serialize(Archive& a, ModelGridComponent& r_c)
{
    a.field("model", r_c.model);
    a.field("count", r_c.count);
    a.field("spacing", r_c.spacing);
}

void serialize(Archive& a, ParentComponent& r_c)
{
    a.field("parent", r_c.parent);
}

void serialize(Archive& a, PrefabInstanceComponent& r_c)
{
    a.field("prefab", r_c.prefab);
}

void serialize(Archive& a, EnvironmentSettings& r_c)
{
    a.field("manual_ev100", r_c.manual_ev100);
}

}
