#pragma once
#include <core/assets/lcollision.h>
#include <editor/viewport/transform_gizmo.h>

namespace lumen {

inline constexpr const char* SHAPE_NAMES[] = { "Box", "Sphere", "Capsule", "Cylinder", "Tapered Cylinder", "Tapered Capsule", "Wedge" };
static_assert(std::size(SHAPE_NAMES) == (size_t)CollisionShapeType::COUNT);

struct ShapeFields
{
    bool rotation = true;
    bool half_extents = false;
    bool radius = false;
    bool top_radius = false;
    bool half_height = false;
};

ShapeFields shape_fields(CollisionShapeType p_kind);
CollisionShapeType shape_kind(CollisionType p_type);
CollisionType shape_collision_type(CollisionShapeType p_kind);

struct ShapeEdit
{
    CollisionShapeType kind = CollisionShapeType::BOX;
    vec3* position = nullptr;
    vec3* rotation = nullptr;
    vec3* half_extents = nullptr;
    float* radius = nullptr;
    float* half_height = nullptr;
    float* top_radius = nullptr;
};

struct ShapeGizmo
{
    TransformGizmo gizmo;
    vec3 _scale = vec3(1.0f);
    vec3 _half_extents = vec3(0.5f);
    float _radius = 0.5f;
    float _half_height = 0.5f;
    float _top_radius = 0.25f;

    static TransformGizmo::Frame frame(const Camera& p_camera, ImVec2 p_pos, ImVec2 p_size);
    bool update(const TransformGizmo::Frame& p_frame, const mat4& p_parent, bool p_world, bool p_hover_ok, const ShapeEdit& p_shape, bool& r_changed);
    void reset();
};

}
