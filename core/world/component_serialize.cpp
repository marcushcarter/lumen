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
