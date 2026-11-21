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
    a.field("tint", r_c.tint);
}

void serialize(Archive& a, CharacterComponent& r_c)
{
    uint32_t controller = (uint32_t)r_c.controller;
    a.field("controller", controller);
    r_c.controller = controller <= (uint32_t)CharacterController::AI ? (CharacterController)controller : CharacterController::NONE;
}

void serialize(Archive& a, OrbitCameraComponent& r_c)
{
    a.field("distance", r_c.distance);
    a.field("shoulder", r_c.shoulder);
    a.field("pivot_height", r_c.pivot_height);
    a.field("radius", r_c.radius);
    a.field("pitch_min", r_c.pitch_min);
    a.field("pitch_max", r_c.pitch_max);
}

void serialize(Archive& a, WaterSourceComponent& r_c)
{
    uint32_t type = (uint32_t)r_c.type;
    a.field("type", type);
    a.field("half_extents", r_c.half_extents);
    a.field("flow", r_c.flow);
    if (!a.loading) return;
    r_c.type = type < (uint32_t)WaterSourceType::COUNT ? (WaterSourceType)type : WaterSourceType::SPRING;
    r_c.half_extents = max(r_c.half_extents, vec3(0.01f));
    r_c.flow = std::max(r_c.flow, 0.0f);
}

void serialize(Archive& a, CollisionComponent& r_c)
{
    uint32_t source = (uint32_t)r_c.source;
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
        s.type = type < (uint32_t)CollisionShapeType::COUNT ? (CollisionShapeType)type : CollisionShapeType::BOX;
        r_c.shapes[kept++] = s;
    }
    if (!a.loading) return;
    r_c.shape_count = kept;
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
    (void)a;
    (void)r_c;
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
}

void serialize(Archive& a, TerrainSettings& r_c)
{
    a.field("terrain", r_c.terrain);
}

void serialize(Archive& a, WaterSettings& r_c)
{
    a.field("enabled", r_c.enabled);
    a.field("cell_size", r_c.cell_size);
    a.field("substeps", r_c.substeps);
    a.field("sim_radius", r_c.sim_radius);
    a.field("lod_error", r_c.lod_error);
    a.field("manning", r_c.manning);
    a.field("vorticity", r_c.vorticity);
    a.field("vortex_min_depth", r_c.vortex_min_depth);
    a.field("cfl_limit", r_c.cfl_limit);
    a.field("flux_depth_cap", r_c.flux_depth_cap);
    a.field("foam_decay", r_c.foam_decay);
    a.field("foam_gain", r_c.foam_gain);
    a.field("bed_probe", r_c.bed_probe);
    a.field("buoyancy", r_c.buoyancy);
    a.field("drag", r_c.drag);
    a.field("push_water", r_c.push_water);
    if (!a.loading) return;
    r_c.cell_size = std::clamp(r_c.cell_size, 0.1f, 2.0f);
    r_c.substeps = std::clamp(r_c.substeps, 1u, 8u);
    r_c.sim_radius = std::clamp(r_c.sim_radius, 8.0f, 512.0f);
    r_c.cfl_limit = std::clamp(r_c.cfl_limit, 0.01f, 0.9f);
    r_c.lod_error = std::clamp(r_c.lod_error, 0.001f, 0.2f);
}

}
