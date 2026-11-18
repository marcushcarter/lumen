#include <core/scene/component_serialize.h>
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

void serialize(Archive& a, LocomotionComponent& r_c)
{
    a.field("locomotion", r_c.locomotion);
    a.field("radius", r_c.radius);
    a.field("height", r_c.height);
    a.field("crouch_height", r_c.crouch_height);
    a.field("model_yaw", r_c.model_yaw);
    a.field("max_step_height", r_c.max_step_height);
    a.field("max_slope", r_c.max_slope);
}

void serialize(Archive& a, OrbitCameraComponent& r_c)
{
    a.field("distance", r_c.distance);
    a.field("aim_distance", r_c.aim_distance);
    a.field("shoulder", r_c.shoulder);
    a.field("aim_shoulder", r_c.aim_shoulder);
    a.field("pivot_height", r_c.pivot_height);
    a.field("radius", r_c.radius);
    a.field("pitch_min", r_c.pitch_min);
    a.field("pitch_max", r_c.pitch_max);
    a.field("sprint_fov", r_c.sprint_fov);
}

void serialize(Archive& a, CollisionComponent& r_c)
{
    static constexpr uint32_t LEGACY_MODEL_SHAPE = 3;
    static constexpr uint32_t SOURCE_UNSET = UINT32_MAX;
    uint32_t source = a.loading ? SOURCE_UNSET : (uint32_t)r_c.source;
    a.field("source", source);
    uint32_t motion = (uint32_t)r_c.motion;
    a.field("motion", motion);
    r_c.motion = motion <= (uint32_t)PhysicsMotion::DYNAMIC ? (PhysicsMotion)motion : PhysicsMotion::STATIC;
    a.field("mass", r_c.mass);
    a.field("friction", r_c.friction);
    a.field("restitution", r_c.restitution);
    a.field("trigger", r_c.trigger);
    uint32_t count = r_c.shape_count;
    a.field("shape_count", count);
    count = std::min(count, COLLISION_MAX_SHAPES);
    bool legacy_model = false;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        const std::string p = "shape" + std::to_string(i) + "_";
        CollisionShapeEntry s = r_c.shapes[i];
        uint32_t type = (uint32_t)s.type;
        a.field((p + "type").c_str(), type);
        a.field((p + "position").c_str(), s.position);
        a.field((p + "rotation").c_str(), s.rotation);
        a.field((p + "half_extents").c_str(), s.half_extents);
        a.field((p + "radius").c_str(), s.radius);
        a.field((p + "half_height").c_str(), s.half_height);
        a.field((p + "top_radius").c_str(), s.top_radius);
        if (!a.loading) continue;
        if (source == SOURCE_UNSET && type == LEGACY_MODEL_SHAPE) {
            legacy_model = true;
            continue;
        }
        s.type = type < (uint32_t)CollisionShapeType::COUNT ? (CollisionShapeType)type : CollisionShapeType::BOX;
        r_c.shapes[kept++] = s;
    }
    if (!a.loading) return;
    r_c.shape_count = kept;
    if (source == SOURCE_UNSET) source = legacy_model || kept == 0 ? (uint32_t)CollisionSource::MESH : (uint32_t)CollisionSource::SHAPES;
    r_c.source = source < (uint32_t)CollisionSource::COUNT ? (CollisionSource)source : CollisionSource::MESH;
}

void serialize(Archive& a, ParentComponent& r_c)
{
    a.field("parent", r_c.parent);
}

void serialize(Archive& a, TemplateInstanceComponent& r_c)
{
    a.field("template", r_c.tmpl);
}

void serialize(Archive& a, EnvironmentSettings& r_c)
{
    a.field("manual_ev100", r_c.manual_ev100);
}

void serialize(Archive& a, ShadowSettings& r_c)
{
    a.field("player_shadow", r_c.player_shadow);
    a.field("cascade0_shadow", r_c.cascade0_shadow);
    a.field("cascade1_shadow", r_c.cascade1_shadow);
    a.field("heightfield_shadow", r_c.heightfield_shadow);
}

void serialize(Archive& a, PhysicsSettings& r_c)
{
    a.field("gravity", r_c.gravity);
    a.field("cloth", r_c.cloth);
    a.field("chains", r_c.chains);
}

void serialize(Archive& a, TerrainSettings& r_c)
{
    a.field("terrain", r_c.terrain);
}

}
