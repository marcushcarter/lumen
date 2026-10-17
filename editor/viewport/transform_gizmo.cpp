#include <editor/viewport/transform_gizmo.h>
#include <drivers/imgui/ui_style.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lumen {

static const vec3 AXIS_UNIT[3] = { vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f) };
static const char* AXIS_LABEL[3] = { "X", "Y", "Z" };

static float _length(ImVec2 p_v)
{
    return std::sqrt(p_v.x * p_v.x + p_v.y * p_v.y);
}

static float _dist_segment(ImVec2 p_p, ImVec2 p_a, ImVec2 p_b)
{
    const ImVec2 ab(p_b.x - p_a.x, p_b.y - p_a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = len2 > 0.0f ? ((p_p.x - p_a.x) * ab.x + (p_p.y - p_a.y) * ab.y) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return _length(ImVec2(p_a.x + ab.x * t - p_p.x, p_a.y + ab.y * t - p_p.y));
}

static float _wrap_angle(float p_a)
{
    while (p_a > pi<float>()) p_a -= two_pi<float>();
    while (p_a < -pi<float>()) p_a += two_pi<float>();
    return p_a;
}

static float _snap(float p_v, float p_step)
{
    return std::round(p_v / p_step) * p_step;
}

static ImU32 _fade(ImU32 p_color, float p_factor)
{
    const float a = (float)((p_color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    return ui_with_alpha(p_color, a * p_factor);
}

static bool _is_scale(TransformGizmo::Handle p_h)
{
    return p_h >= TransformGizmo::Handle::SCALE_X && p_h <= TransformGizmo::Handle::SCALE_Z;
}

static bool _is_rotate(TransformGizmo::Handle p_h)
{
    return p_h >= TransformGizmo::Handle::ROTATE_X && p_h <= TransformGizmo::Handle::ROTATE_Z;
}

static bool _is_move_axis(TransformGizmo::Handle p_h)
{
    return p_h >= TransformGizmo::Handle::MOVE_X && p_h <= TransformGizmo::Handle::MOVE_Z;
}

static TransformGizmo::Handle _offset(TransformGizmo::Handle p_first, uint32_t p_i)
{
    return (TransformGizmo::Handle)((uint32_t)p_first + p_i);
}

static uint32_t _index(TransformGizmo::Handle p_h, TransformGizmo::Handle p_first)
{
    return (uint32_t)p_h - (uint32_t)p_first;
}

void TransformGizmo::reset()
{
    hovered = Handle::NONE;
    active = Handle::NONE;
}

bool TransformGizmo::_project(const Frame& p_frame, vec3 p_world, ImVec2& r_screen) const
{
    const vec4 clip = p_frame.view_proj * vec4(p_world, 1.0f);
    if (clip.w <= p_frame.near_z) return false;
    const float x = clip.x / clip.w;
    const float y = clip.y / clip.w;
    r_screen = ImVec2(p_frame.min.x + (x * 0.5f + 0.5f) * p_frame.size.x, p_frame.min.y + (0.5f - y * 0.5f) * p_frame.size.y);
    return true;
}

vec3 TransformGizmo::_ray_dir(const Frame& p_frame, ImVec2 p_mouse) const
{
    const float x = (p_mouse.x - p_frame.min.x) / p_frame.size.x * 2.0f - 1.0f;
    const float y = 1.0f - (p_mouse.y - p_frame.min.y) / p_frame.size.y * 2.0f;
    return normalize(p_frame.cam_rotation * vec3(x / p_frame.proj_x, y / p_frame.proj_y, -1.0f));
}

bool TransformGizmo::_ray_plane(const Frame& p_frame, ImVec2 p_mouse, vec3 p_point, vec3 p_normal, vec3& r_hit) const
{
    const vec3 dir = _ray_dir(p_frame, p_mouse);
    const float denom = dot(dir, p_normal);
    if (std::abs(denom) < 1e-6f) return false;
    const float t = dot(p_point - p_frame.cam_position, p_normal) / denom;
    if (t <= 0.0f) return false;
    r_hit = p_frame.cam_position + dir * t;
    return true;
}

void TransformGizmo::_build_shape(const Frame& p_frame, vec3 p_pivot, float p_world_px, Shape& r_shape) const
{
    const UiStyle::Gizmo& g = ui_style().gizmo;
    const float arrow = g.arrow_ratio * p_frame.size.y * p_world_px;
    const float ring = arrow * g.ring_scale;
    const float handle = arrow * g.scale_handle_scale;

    _project(p_frame, p_pivot, r_shape.center);
    for (uint32_t i = 0; i < 3; i++) {
        r_shape.tip_visible[i] = _project(p_frame, p_pivot + axes[i] * arrow, r_shape.tip[i]);
        r_shape.scale_visible[i] = _project(p_frame, p_pivot + axes[i] * handle, r_shape.scale[i]);
        const vec3 u = axes[(i + 1) % 3];
        const vec3 v = axes[(i + 2) % 3];
        for (uint32_t s = 0; s < RING_SEGMENTS; s++) {
            const float t = two_pi<float>() * (float)s / (float)RING_SEGMENTS;
            r_shape.ring_visible[i][s] = _project(p_frame, p_pivot + (u * std::cos(t) + v * std::sin(t)) * ring, r_shape.ring[i][s]);
        }
    }
}

TransformGizmo::Handle TransformGizmo::_hit_test(const Shape& p_shape, ImVec2 p_mouse, bool ) const
{
    const UiStyle::Gizmo& g = ui_style().gizmo;
    Handle best = Handle::NONE;
    float best_d = FLT_MAX;

    for (uint32_t i = 0; i < 3; i++) {
        if (!p_shape.scale_visible[i]) continue;
        const float d = std::max(std::abs(p_mouse.x - p_shape.scale[i].x), std::abs(p_mouse.y - p_shape.scale[i].y));
        if (d <= g.handle_size * 0.5f + g.hit_tolerance && d < best_d) {
            best = _offset(Handle::SCALE_X, i);
            best_d = d;
        }
    }
    if (best != Handle::NONE) return best;

    if (_length(ImVec2(p_mouse.x - p_shape.center.x, p_mouse.y - p_shape.center.y)) <= g.center_radius + g.hit_tolerance) return Handle::MOVE_SCREEN;

    for (uint32_t i = 0; i < 3; i++) {
        if (!p_shape.tip_visible[i]) continue;
        const float d = _dist_segment(p_mouse, p_shape.center, p_shape.tip[i]);
        if (d <= g.head_width * 0.5f + g.hit_tolerance && d < best_d) {
            best = _offset(Handle::MOVE_X, i);
            best_d = d;
        }
    }
    if (best != Handle::NONE) return best;

    for (uint32_t i = 0; i < 3; i++) {
        for (uint32_t s = 0; s < RING_SEGMENTS; s++) {
            const uint32_t n = (s + 1) % RING_SEGMENTS;
            if (!p_shape.ring_visible[i][s] || !p_shape.ring_visible[i][n]) continue;
            const float d = _dist_segment(p_mouse, p_shape.ring[i][s], p_shape.ring[i][n]);
            if (d <= g.ring_thickness * 0.5f + g.hit_tolerance && d < best_d) {
                best = _offset(Handle::ROTATE_X, i);
                best_d = d;
            }
        }
    }
    return best;
}

void TransformGizmo::_begin(const Frame& p_frame, const Shape& p_shape, vec3 p_pivot, const TransformComponent& p_xf, ImVec2 p_mouse)
{
    start_position = p_xf.position;
    start_rotation = p_xf.rotation;
    start_scale = p_xf.scale;
    start_mouse = p_mouse;
    start_hit = p_pivot;

    const vec3 forward = p_frame.cam_rotation * vec3(0.0f, 0.0f, -1.0f);
    const vec3 to_cam = p_frame.cam_position - p_pivot;

    if (_is_move_axis(active)) {
        drag_axis = axes[_index(active, Handle::MOVE_X)];
        vec3 n = to_cam - drag_axis * dot(to_cam, drag_axis);
        drag_plane_normal = length(n) > 1e-5f ? normalize(n) : -forward;
        _ray_plane(p_frame, p_mouse, p_pivot, drag_plane_normal, start_hit);
    } else if (active == Handle::MOVE_SCREEN) {
        drag_plane_normal = -forward;
        _ray_plane(p_frame, p_mouse, p_pivot, drag_plane_normal, start_hit);
    } else if (_is_rotate(active)) {
        drag_axis = axes[_index(active, Handle::ROTATE_X)];
        rotate_sign = dot(drag_axis, to_cam) >= 0.0f ? 1.0f : -1.0f;
        rotate_prev = std::atan2(p_mouse.y - p_shape.center.y, p_mouse.x - p_shape.center.x);
        rotate_total = 0.0f;
    } else if (_is_scale(active)) {
        const uint32_t i = _index(active, Handle::SCALE_X);
        ImVec2 dir(p_shape.scale[i].x - p_shape.center.x, p_shape.scale[i].y - p_shape.center.y);
        const float len = _length(dir);
        scale_dir = len > 1.0f ? ImVec2(dir.x / len, dir.y / len) : ImVec2(1.0f, 0.0f);
        scale_len = len > 1.0f ? len : 1.0f;

        float best = -1.0f;
        for (uint32_t k = 0; k < 3; k++) {
            const float d = std::abs(dot(axes[i], start_rotation * AXIS_UNIT[k]));
            if (d > best) {
                best = d;
                scale_axis = k;
            }
        }
    }
}

bool TransformGizmo::_drag(const Frame& p_frame, const Shape& p_shape, ImVec2 p_mouse, bool p_snap, bool p_uniform, TransformComponent& r_xf)
{
    TransformComponent next = r_xf;

    if (_is_move_axis(active)) {
        vec3 hit;
        if (!_ray_plane(p_frame, p_mouse, start_position, drag_plane_normal, hit)) return false;
        float d = dot(hit - start_hit, drag_axis);
        if (p_snap) d = _snap(d, MOVE_SNAP);
        next.position = start_position + drag_axis * d;
    } else if (active == Handle::MOVE_SCREEN) {
        vec3 hit;
        if (!_ray_plane(p_frame, p_mouse, start_position, drag_plane_normal, hit)) return false;
        vec3 d = hit - start_hit;
        if (p_snap) d = vec3(_snap(d.x, MOVE_SNAP), _snap(d.y, MOVE_SNAP), _snap(d.z, MOVE_SNAP));
        next.position = start_position + d;
    } else if (_is_rotate(active)) {
        const float a = std::atan2(p_mouse.y - p_shape.center.y, p_mouse.x - p_shape.center.x);
        rotate_total += _wrap_angle(a - rotate_prev);
        rotate_prev = a;
        float angle = -rotate_total * rotate_sign;
        if (p_snap) angle = radians(_snap(degrees(angle), ROTATE_SNAP));
        next.rotation = normalize(angleAxis(angle, drag_axis) * start_rotation);
    } else if (_is_scale(active)) {
        const float t = ((p_mouse.x - start_mouse.x) * scale_dir.x + (p_mouse.y - start_mouse.y) * scale_dir.y) / scale_len;
        const float factor = std::max(1.0f + t, MIN_SCALE_FACTOR);
        next.scale = start_scale;
        for (uint32_t k = 0; k < 3; k++) {
            if (!p_uniform && k != scale_axis) continue;
            float s = start_scale[k] * factor;
            const float sign = s < 0.0f ? -1.0f : 1.0f;
            float mag = std::abs(s);
            if (p_snap) mag = std::max(_snap(mag, SCALE_SNAP), SCALE_SNAP);
            next.scale[k] = sign * std::max(mag, MIN_SCALE);
        }
    }

    bool finite = true;
    for (int i = 0; i < 3; i++) finite = finite && std::isfinite(next.position[i]) && std::isfinite(next.scale[i]);
    for (int i = 0; i < 4; i++) finite = finite && std::isfinite(next.rotation[i]);
    if (!finite) return false;

    const bool changed = next.position != r_xf.position || next.rotation != r_xf.rotation || next.scale != r_xf.scale;
    r_xf = next;
    return changed;
}

void TransformGizmo::_draw(ImDrawList* p_dl, const Shape& p_shape, bool p_uniform) const
{
    const UiStyle& ui = ui_style();
    const UiStyle::Gizmo& g = ui.gizmo;
    const ImU32 axis_color[3] = { ui.color.gizmo_x, ui.color.gizmo_y, ui.color.gizmo_z };
    const Handle focus = active != Handle::NONE ? active : hovered;

    auto hot = [&](Handle p_h) {
        if (focus == p_h) return true;
        return p_uniform && _is_scale(focus) && _is_scale(p_h);
    };
    auto color = [&](Handle p_h, ImU32 p_base) {
        if (hot(p_h)) return ui.color.gizmo_hot;
        return active != Handle::NONE ? _fade(p_base, g.drag_alpha) : p_base;
    };

    for (uint32_t i = 0; i < 3; i++) {
        const ImU32 col = color(_offset(Handle::ROTATE_X, i), axis_color[i]);
        for (uint32_t s = 0; s < RING_SEGMENTS; s++) {
            const uint32_t n = (s + 1) % RING_SEGMENTS;
            if (p_shape.ring_visible[i][s] && p_shape.ring_visible[i][n]) p_dl->AddLine(p_shape.ring[i][s], p_shape.ring[i][n], col, g.ring_thickness);
        }
    }

    for (uint32_t i = 0; i < 3; i++) {
        if (!p_shape.tip_visible[i]) continue;
        const ImU32 col = color(_offset(Handle::MOVE_X, i), axis_color[i]);
        const ImVec2 dir(p_shape.tip[i].x - p_shape.center.x, p_shape.tip[i].y - p_shape.center.y);
        const float len = _length(dir);
        if (len < 1.0f) continue;
        const ImVec2 n(dir.x / len, dir.y / len);
        if (len > g.head_length) {
            const ImVec2 base(p_shape.tip[i].x - n.x * g.head_length, p_shape.tip[i].y - n.y * g.head_length);
            const ImVec2 side(-n.y * g.head_width * 0.5f, n.x * g.head_width * 0.5f);
            const ImVec2 join(base.x + n.x * g.head_length * 0.5f, base.y + n.y * g.head_length * 0.5f);
            p_dl->AddLine(p_shape.center, join, col, g.axis_thickness);
            p_dl->AddTriangleFilled(p_shape.tip[i], ImVec2(base.x + side.x, base.y + side.y), ImVec2(base.x - side.x, base.y - side.y), col);
        } else {
            p_dl->AddLine(p_shape.center, p_shape.tip[i], col, g.axis_thickness);
        }
        const ImVec2 text = ImGui::CalcTextSize(AXIS_LABEL[i]);
        const ImVec2 label(p_shape.tip[i].x + n.x * g.label_offset - text.x * 0.5f, p_shape.tip[i].y + n.y * g.label_offset - text.y * 0.5f);
        p_dl->AddText(label, col, AXIS_LABEL[i]);
    }

    const float half = g.handle_size * 0.5f;
    for (uint32_t i = 0; i < 3; i++) {
        if (!p_shape.scale_visible[i]) continue;
        const ImU32 col = color(_offset(Handle::SCALE_X, i), axis_color[i]);
        const ImVec2 c = p_shape.scale[i];
        p_dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), col);
    }

    p_dl->AddCircleFilled(p_shape.center, g.center_radius, color(Handle::MOVE_SCREEN, ui.color.gizmo_center), 20);
}

bool TransformGizmo::update(const Frame& p_frame, bool p_world, bool p_hover_ok, TransformComponent& r_xf, bool& r_changed)
{
    r_changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    const vec3 pivot = r_xf.position;

    const vec4 clip = p_frame.view_proj * vec4(pivot, 1.0f);
    if (clip.w <= p_frame.near_z) {
        reset();
        return false;
    }

    for (uint32_t i = 0; i < 3; i++) axes[i] = p_world ? AXIS_UNIT[i] : normalize(r_xf.rotation * AXIS_UNIT[i]);
    const float world_px = 2.0f * clip.w / (p_frame.proj_y * p_frame.size.y);

    Shape shape;
    _build_shape(p_frame, pivot, world_px, shape);

    const ImVec2 mouse = io.MousePos;
    const bool uniform = io.KeyShift;

    if (active != Handle::NONE) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            r_xf.position = start_position;
            r_xf.rotation = start_rotation;
            r_xf.scale = start_scale;
            r_changed = true;
            active = Handle::NONE;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            r_changed = _drag(p_frame, shape, mouse, io.KeyCtrl, uniform, r_xf);
            if (r_changed) {
                for (uint32_t i = 0; i < 3; i++) axes[i] = p_world ? AXIS_UNIT[i] : normalize(r_xf.rotation * AXIS_UNIT[i]);
                _build_shape(p_frame, r_xf.position, world_px, shape);
            }
        } else {
            active = Handle::NONE;
        }
    } else {
        hovered = p_hover_ok ? _hit_test(shape, mouse, uniform) : Handle::NONE;
        if (hovered != Handle::NONE && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            active = hovered;
            _begin(p_frame, shape, pivot, r_xf, mouse);
        }
    }

    _draw(ImGui::GetWindowDrawList(), shape, uniform);
    return hovered != Handle::NONE || active != Handle::NONE;
}

}