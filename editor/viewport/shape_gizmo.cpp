#include <editor/viewport/shape_gizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace lumen {

static constexpr float MIN_SIZE = 0.001f;

ShapeFields shape_fields(CollisionShapeType p_kind)
{
    switch (p_kind) {
        case CollisionShapeType::SPHERE: return { .rotation = false, .radius = true };
        case CollisionShapeType::CAPSULE:
        case CollisionShapeType::CYLINDER: return { .radius = true, .half_height = true };
        case CollisionShapeType::TAPERED_CYLINDER:
        case CollisionShapeType::TAPERED_CAPSULE: return { .radius = true, .top_radius = true, .half_height = true };
        default: return { .half_extents = true };
    }
}

CollisionShapeType shape_kind(CollisionType p_type)
{
    switch (p_type) {
        case CollisionType::SPHERE: return CollisionShapeType::SPHERE;
        case CollisionType::CAPSULE: return CollisionShapeType::CAPSULE;
        case CollisionType::CYLINDER: return CollisionShapeType::CYLINDER;
        case CollisionType::TAPERED_CYLINDER: return CollisionShapeType::TAPERED_CYLINDER;
        case CollisionType::TAPERED_CAPSULE: return CollisionShapeType::TAPERED_CAPSULE;
        case CollisionType::WEDGE: return CollisionShapeType::WEDGE;
        default: return CollisionShapeType::BOX;
    }
}

CollisionType shape_collision_type(CollisionShapeType p_kind)
{
    switch (p_kind) {
        case CollisionShapeType::SPHERE: return CollisionType::SPHERE;
        case CollisionShapeType::CAPSULE: return CollisionType::CAPSULE;
        case CollisionShapeType::CYLINDER: return CollisionType::CYLINDER;
        case CollisionShapeType::TAPERED_CYLINDER: return CollisionType::TAPERED_CYLINDER;
        case CollisionShapeType::TAPERED_CAPSULE: return CollisionType::TAPERED_CAPSULE;
        case CollisionShapeType::WEDGE: return CollisionType::WEDGE;
        default: return CollisionType::BOX;
    }
}

TransformGizmo::Frame ShapeGizmo::frame(const Camera& p_camera, ImVec2 p_pos, ImVec2 p_size)
{
    const mat4 proj = p_camera.proj(p_size.x / std::max(p_size.y, 1.0f));
    TransformGizmo::Frame f;
    f.view_proj = proj * p_camera.view();
    f.cam_position = p_camera.position;
    f.cam_rotation = p_camera.rotation;
    f.near_z = p_camera.near_z;
    f.proj_x = proj[0][0];
    f.proj_y = proj[1][1];
    f.min = p_pos;
    f.size = p_size;
    return f;
}

void ShapeGizmo::reset()
{
    gizmo.reset();
    _scale = vec3(1.0f);
}

static quat _rotation_of(const mat4& p_m)
{
    const vec3 x = normalize(vec3(p_m[0]));
    const vec3 y = normalize(vec3(p_m[1]));
    const vec3 z = normalize(vec3(p_m[2]));
    return normalize(quat_cast(mat3(x, y, z)));
}

bool ShapeGizmo::update(const TransformGizmo::Frame& p_frame, const mat4& p_parent, bool p_world, bool p_hover_ok, const ShapeEdit& p_shape, bool& r_changed)
{
    r_changed = false;
    if (gizmo.active == TransformGizmo::Handle::NONE) {
        _scale = vec3(1.0f);
        _half_extents = *p_shape.half_extents;
        _radius = *p_shape.radius;
        _half_height = *p_shape.half_height;
        _top_radius = *p_shape.top_radius;
    }
    const quat local_rotation = quat(radians(*p_shape.rotation));
    const quat parent_rotation = _rotation_of(p_parent);
    TransformComponent xf;
    xf.position = vec3(p_parent * vec4(*p_shape.position, 1.0f));
    xf.rotation = normalize(parent_rotation * local_rotation);
    xf.scale = _scale;

    bool changed = false;
    const bool hot = gizmo.update(p_frame, p_world, p_hover_ok, xf, changed);
    if (!changed) return hot;

    *p_shape.position = vec3(inverse(p_parent) * vec4(xf.position, 1.0f));
    if (shape_fields(p_shape.kind).rotation) *p_shape.rotation = degrees(eulerAngles(normalize(inverse(parent_rotation) * xf.rotation)));
    _scale = max(xf.scale, vec3(TransformGizmo::MIN_SCALE));
    const ShapeFields fields = shape_fields(p_shape.kind);
    const float round = p_shape.kind == CollisionShapeType::SPHERE ? std::max(_scale.x, std::max(_scale.y, _scale.z)) : std::max(_scale.x, _scale.z);
    if (fields.half_extents) *p_shape.half_extents = max(_half_extents * _scale, vec3(MIN_SIZE));
    if (fields.radius) *p_shape.radius = std::max(_radius * round, MIN_SIZE);
    if (fields.top_radius) *p_shape.top_radius = std::max(_top_radius * round, MIN_SIZE);
    if (fields.half_height) *p_shape.half_height = std::max(_half_height * _scale.y, MIN_SIZE);
    r_changed = true;
    return hot;
}

}
