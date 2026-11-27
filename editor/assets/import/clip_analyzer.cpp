#include <editor/assets/import/clip_analyzer.h>
#include <core/animation/clip_player.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace lumen {

static constexpr float REFERENCE_HIP_HEIGHT = 0.95f;
static constexpr float CONTACT_HEIGHT = 0.045f;
static constexpr float CONTACT_SPEED = 0.5f;
static constexpr float CONTACT_VERTICAL_SPEED = 0.3f;
static constexpr uint32_t CONTACT_GAP_FRAMES = 2;
static constexpr uint32_t CONTACT_MIN_FRAMES = 3;
static constexpr float TRAJECTORY_SMOOTH_S = 0.1f;
static constexpr float ROOT_MOTION_MIN = 0.05f;
static constexpr float ROOT_TURN_MIN = 5.0f;

std::vector<std::string> clip_name_tokens(std::string_view p_name)
{
    std::vector<std::string> tokens;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) tokens.push_back(cur);
        cur.clear();
    };
    for (size_t i = 0; i < p_name.size(); ++i) {
        const unsigned char c = (unsigned char)p_name[i];
        if (!std::isalnum(c)) {
            flush();
            continue;
        }
        if (i > 0 && std::isupper(c) && std::islower((unsigned char)p_name[i - 1])) flush();
        cur.push_back((char)std::tolower(c));
    }
    flush();
    return tokens;
}

static bool _starts(const std::string& p_token, std::string_view p_word)
{
    return p_token.size() >= p_word.size() && std::string_view(p_token).substr(0, p_word.size()) == p_word;
}

static bool _direction(const std::string& p_token, float& r_degrees)
{
    struct Named { std::string_view name; float degrees; };
    static constexpr Named NAMED[] = {
        { "f", 0.0f }, { "fwd", 0.0f }, { "forward", 0.0f },
        { "b", 180.0f }, { "bwd", 180.0f }, { "back", 180.0f }, { "backward", 180.0f }, { "backwards", 180.0f },
        { "l", -90.0f }, { "left", -90.0f }, { "r", 90.0f }, { "right", 90.0f },
        { "fl", -45.0f }, { "fr", 45.0f }, { "bl", -135.0f }, { "br", 135.0f },
    };
    for (const Named& n : NAMED) {
        if (p_token == n.name) {
            r_degrees = n.degrees;
            return true;
        }
    }
    if (p_token.size() < 2) return false;
    const char front = p_token.front();
    const char back = p_token.back();
    const bool lead = front == 'l' || front == 'r';
    const bool trail = back == 'l' || back == 'r';
    if (lead == trail) return false;
    const std::string digits = lead ? p_token.substr(1) : p_token.substr(0, p_token.size() - 1);
    if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit((unsigned char)c); })) return false;
    const float angle = (float)std::atoi(digits.c_str());
    r_degrees = (lead ? front : back) == 'l' ? -angle : angle;
    return true;
}

void clip_name_tags(std::string_view p_name, ClipMeta& r_meta)
{
    struct Word { std::string_view word; ClipGait gait; ClipRole role; };
    static constexpr Word WORDS[] = {
        { "walk", ClipGait::WALK, ClipRole::NONE },
        { "jog", ClipGait::JOG, ClipRole::NONE },
        { "run", ClipGait::RUN, ClipRole::NONE },
        { "sprint", ClipGait::SPRINT, ClipRole::NONE },
        { "crouch", ClipGait::CROUCH, ClipRole::NONE },
        { "idle", ClipGait::NONE, ClipRole::IDLE },
        { "start", ClipGait::NONE, ClipRole::START },
        { "stop", ClipGait::NONE, ClipRole::STOP },
        { "pivot", ClipGait::NONE, ClipRole::PIVOT },
        { "turn", ClipGait::NONE, ClipRole::TURN },
        { "jump", ClipGait::NONE, ClipRole::JUMP },
        { "land", ClipGait::NONE, ClipRole::LAND },
        { "loop", ClipGait::NONE, ClipRole::CYCLE },
        { "cycle", ClipGait::NONE, ClipRole::CYCLE },
    };
    r_meta.role = ClipRole::NONE;
    r_meta.gait = ClipGait::NONE;
    r_meta.has_direction = false;
    r_meta.direction = 0.0f;
    for (const std::string& token : clip_name_tokens(p_name)) {
        float degrees = 0.0f;
        if (!r_meta.has_direction && _direction(token, degrees)) {
            r_meta.has_direction = true;
            r_meta.direction = degrees;
            continue;
        }
        for (const Word& w : WORDS) {
            if (!_starts(token, w.word)) continue;
            if (w.gait != ClipGait::NONE && r_meta.gait == ClipGait::NONE) r_meta.gait = w.gait;
            if (w.role != ClipRole::NONE && r_meta.role == ClipRole::NONE) r_meta.role = w.role;
            break;
        }
    }
    if (r_meta.role == ClipRole::NONE && r_meta.gait != ClipGait::NONE) r_meta.role = ClipRole::CYCLE;
}

static bool _has_token(const std::vector<std::string>& p_tokens, std::string_view p_a, std::string_view p_b)
{
    for (const std::string& t : p_tokens) {
        if (t == p_a || t == p_b) return true;
    }
    return false;
}

static bool _name_has(const std::string& p_lower, std::string_view p_part)
{
    return p_lower.find(p_part) != std::string::npos;
}

static std::string _lower(const std::string& p_name)
{
    std::string out = p_name;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static uint32_t _depth(const LSkeleton& p_skeleton, int32_t p_joint)
{
    uint32_t depth = 0;
    for (int32_t j = p_skeleton.parents[p_joint]; j >= 0; j = p_skeleton.parents[j]) depth++;
    return depth;
}

static void _find_feet(const LSkeleton& p_skeleton, int32_t r_foot[2], int32_t r_toe[2])
{
    static constexpr std::string_view EXCLUDE[] = { "toe", "ball", "end", "twist", "ik", "pole", "target", "roll", "heel" };
    const uint32_t count = p_skeleton.joint_count();
    for (int s = 0; s < 2; ++s) {
        r_foot[s] = SKELETON_NO_JOINT;
        r_toe[s] = SKELETON_NO_JOINT;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const std::string lower = _lower(p_skeleton.names[i]);
        if (!_name_has(lower, "foot") && !_name_has(lower, "ankle")) continue;
        bool excluded = false;
        for (std::string_view e : EXCLUDE) excluded = excluded || _name_has(lower, e);
        if (excluded) continue;
        const std::vector<std::string> tokens = clip_name_tokens(p_skeleton.names[i]);
        const bool left = _has_token(tokens, "l", "left") || _name_has(lower, "left");
        const bool right = _has_token(tokens, "r", "right") || _name_has(lower, "right");
        if (left == right) continue;
        const int side = left ? 0 : 1;
        if (r_foot[side] == SKELETON_NO_JOINT || _depth(p_skeleton, (int32_t)i) < _depth(p_skeleton, r_foot[side])) r_foot[side] = (int32_t)i;
    }
    for (int s = 0; s < 2; ++s) {
        if (r_foot[s] == SKELETON_NO_JOINT) continue;
        int32_t first = SKELETON_NO_JOINT;
        for (uint32_t i = 0; i < count; ++i) {
            if (p_skeleton.parents[i] != r_foot[s]) continue;
            if (first == SKELETON_NO_JOINT) first = (int32_t)i;
            const std::string lower = _lower(p_skeleton.names[i]);
            if (_name_has(lower, "toe") || _name_has(lower, "ball")) {
                r_toe[s] = (int32_t)i;
                break;
            }
        }
        if (r_toe[s] == SKELETON_NO_JOINT) r_toe[s] = first;
    }
}

static int32_t _root(const LSkeleton& p_skeleton, std::vector<uint32_t>& r_size)
{
    const uint32_t count = p_skeleton.joint_count();
    r_size.assign(count, 1u);
    for (int32_t i = (int32_t)count - 1; i >= 0; --i) {
        if (p_skeleton.parents[i] >= 0) r_size[p_skeleton.parents[i]] += r_size[i];
    }
    int32_t best = SKELETON_NO_JOINT;
    for (uint32_t i = 0; i < count; ++i) {
        if (p_skeleton.parents[i] < 0 && (best == SKELETON_NO_JOINT || r_size[i] > r_size[best])) best = (int32_t)i;
    }
    return best;
}

static int32_t _hips(const LSkeleton& p_skeleton, int32_t p_root, const std::vector<uint32_t>& p_size)
{
    const uint32_t count = p_skeleton.joint_count();
    for (uint32_t i = 0; i < count; ++i) {
        const std::string lower = _lower(p_skeleton.names[i]);
        if (_name_has(lower, "pelvis") || _name_has(lower, "hips")) return (int32_t)i;
    }
    int32_t best = SKELETON_NO_JOINT;
    for (uint32_t i = 0; i < count; ++i) {
        if (p_skeleton.parents[i] == p_root && (best == SKELETON_NO_JOINT || p_size[i] > p_size[best])) best = (int32_t)i;
    }
    return best == SKELETON_NO_JOINT ? p_root : best;
}

static quat _rotation(const mat4& p_m)
{
    mat3 r(p_m);
    for (int c = 0; c < 3; ++c) {
        const float len = length(r[c]);
        if (len > 1e-6f) r[c] /= len;
    }
    return normalize(quat_cast(r));
}

static float _path_xz(const std::vector<vec3>& p_points)
{
    float total = 0.0f;
    for (size_t i = 1; i < p_points.size(); ++i) total += length(vec2(p_points[i].x - p_points[i - 1].x, p_points[i].z - p_points[i - 1].z));
    return total;
}

static float _yaw_span(const std::vector<quat>& p_rotations)
{
    if (p_rotations.empty()) return 0.0f;
    const quat inv0 = inverse(p_rotations.front());
    float span = 0.0f;
    for (const quat& q : p_rotations) {
        const vec3 fwd = (q * inv0) * vec3(0.0f, 0.0f, 1.0f);
        span = std::max(span, std::abs(std::atan2(fwd.x, fwd.z)));
    }
    return span;
}

static void _contacts(const std::vector<bool>& p_raw, std::vector<ClipContact>& r_out)
{
    std::vector<bool> on = p_raw;
    const uint32_t count = (uint32_t)on.size();
    for (uint32_t f = 0; f < count;) {
        if (on[f]) {
            f++;
            continue;
        }
        uint32_t end = f;
        while (end < count && !on[end]) end++;
        if (f > 0 && end < count && end - f <= CONTACT_GAP_FRAMES) {
            for (uint32_t i = f; i < end; ++i) on[i] = true;
        }
        f = end;
    }
    r_out.clear();
    for (uint32_t f = 0; f < count;) {
        if (!on[f]) {
            f++;
            continue;
        }
        uint32_t end = f;
        while (end < count && on[end]) end++;
        if (end - f >= CONTACT_MIN_FRAMES || count < CONTACT_MIN_FRAMES * 2) r_out.push_back({ f, end - 1 });
        f = end;
    }
}

static void _phase(const ClipMeta& p_meta, uint32_t p_frames, std::vector<float>& r_phase)
{
    struct Marker { uint32_t frame; int side; };
    std::vector<Marker> markers;
    bool sides[2] = { false, false };
    for (int s = 0; s < 2; ++s) {
        for (const ClipContact& c : p_meta.feet[s].contacts) {
            if (c.start == 0) continue;
            markers.push_back({ c.start, s });
            sides[s] = true;
        }
    }
    r_phase.clear();
    if (markers.size() < 2 || !sides[0] || !sides[1]) return;
    std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.frame < b.frame; });

    std::vector<float> value(markers.size());
    value[0] = markers[0].side == 0 ? 0.0f : 0.5f;
    for (size_t i = 1; i < markers.size(); ++i) value[i] = value[i - 1] + (markers[i].side == markers[i - 1].side ? 1.0f : 0.5f);

    r_phase.resize(p_frames);
    for (uint32_t f = 0; f < p_frames; ++f) {
        size_t seg = 0;
        while (seg + 2 < markers.size() && f >= markers[seg + 1].frame) seg++;
        const float f0 = (float)markers[seg].frame;
        const float f1 = (float)markers[seg + 1].frame;
        const float rate = f1 > f0 ? (value[seg + 1] - value[seg]) / (f1 - f0) : 0.0f;
        const float u = value[seg] + ((float)f - f0) * rate;
        r_phase[f] = u - std::floor(u);
    }
}

void clip_analyze(LAnimation& r_clip, const LSkeleton* p_skeleton, std::string_view p_name)
{
    ClipMeta meta;
    clip_name_tags(p_name, meta);
    r_clip.meta = meta;
    const uint32_t frames = r_clip.frame_count;
    if (!p_skeleton || p_skeleton->joint_count() == 0 || frames == 0) return;
    const LSkeleton& skeleton = *p_skeleton;
    const uint32_t joints = skeleton.joint_count();

    std::vector<int32_t> map;
    clip_track_map(skeleton, r_clip, map);
    std::vector<uint32_t> size;
    const int32_t root = _root(skeleton, size);
    if (root == SKELETON_NO_JOINT) return;
    const int32_t hips = _hips(skeleton, root, size);
    int32_t foot[2], toe[2];
    _find_feet(skeleton, foot, toe);

    std::vector<JointTransform> local(joints);
    std::vector<mat4> global(joints);
    std::vector<vec3> root_pos(frames), hips_pos(frames);
    std::vector<quat> root_rot(frames), hips_rot(frames);
    std::vector<vec3> points[4];
    const int32_t point_joint[4] = { foot[0], toe[0], foot[1], toe[1] };
    for (std::vector<vec3>& p : points) p.resize(frames);
    for (uint32_t f = 0; f < frames; ++f) {
        local = skeleton.rest;
        for (uint32_t t = 0; t < (uint32_t)map.size(); ++t) {
            if (map[t] != SKELETON_NO_JOINT) local[map[t]] = r_clip.samples[(size_t)t * frames + f];
        }
        skeleton.globals(local.data(), global.data());
        root_pos[f] = vec3(global[root][3]);
        root_rot[f] = _rotation(global[root]);
        hips_pos[f] = vec3(global[hips][3]);
        hips_rot[f] = _rotation(global[hips]);
        for (int p = 0; p < 4; ++p) {
            if (point_joint[p] != SKELETON_NO_JOINT) points[p][f] = vec3(global[point_joint[p]][3]);
        }
    }

    const bool use_root = root != hips && (_path_xz(root_pos) > ROOT_MOTION_MIN || _yaw_span(root_rot) > glm::radians(ROOT_TURN_MIN));
    const std::vector<vec3>& pos = use_root ? root_pos : hips_pos;
    const std::vector<quat>& rot = use_root ? root_rot : hips_rot;
    r_clip.meta.root_joint = skeleton.names[use_root ? root : hips];

    const int32_t window = std::max(0, (int32_t)std::lround(TRAJECTORY_SMOOTH_S * r_clip.sample_rate));
    std::vector<float> yaw(frames, 0.0f);
    const quat inv0 = inverse(rot[0]);
    for (uint32_t f = 0; f < frames; ++f) {
        const vec3 fwd = (rot[f] * inv0) * vec3(0.0f, 0.0f, 1.0f);
        float y = std::atan2(fwd.x, fwd.z);
        if (f > 0) {
            while (y - yaw[f - 1] > glm::pi<float>()) y -= glm::two_pi<float>();
            while (y - yaw[f - 1] < -glm::pi<float>()) y += glm::two_pi<float>();
        }
        yaw[f] = y;
    }
    r_clip.meta.trajectory.resize(frames);
    for (int32_t f = 0; f < (int32_t)frames; ++f) {
        const int32_t a = std::max(0, f - window);
        const int32_t b = std::min((int32_t)frames - 1, f + window);
        vec2 xz(0.0f);
        float y = 0.0f;
        for (int32_t i = a; i <= b; ++i) {
            xz += vec2(pos[i].x, pos[i].z);
            y += yaw[i];
        }
        const float n = (float)(b - a + 1);
        r_clip.meta.trajectory[f] = vec4(xz.x / n, 0.0f, xz.y / n, y / n);
    }
    const vec4 first = r_clip.meta.trajectory.front();
    for (vec4& t : r_clip.meta.trajectory) t.w -= first.w;

    r_clip.meta.traveled.resize(frames);
    r_clip.meta.traveled[0] = 0.0f;
    for (uint32_t f = 1; f < frames; ++f) {
        const vec4& a = r_clip.meta.trajectory[f - 1];
        const vec4& b = r_clip.meta.trajectory[f];
        r_clip.meta.traveled[f] = r_clip.meta.traveled[f - 1] + length(vec2(b.x - a.x, b.z - a.z));
    }
    const vec4& last = r_clip.meta.trajectory.back();
    r_clip.meta.distance = r_clip.meta.traveled.back();
    r_clip.meta.speed = r_clip.duration > 0.0f ? r_clip.meta.distance / r_clip.duration : 0.0f;
    r_clip.meta.turn = -glm::degrees(last.w);
    r_clip.meta.displacement = vec3(last.x - first.x, pos.back().y - pos.front().y, last.z - first.z);
    if (r_clip.meta.has_root_motion()) r_clip.meta.move_direction = glm::degrees(std::atan2(-r_clip.meta.displacement.x, r_clip.meta.displacement.z));

    float hip_height = 0.0f;
    {
        std::vector<mat4> rest(joints);
        skeleton.globals(skeleton.rest.data(), rest.data());
        float floor = vec3(rest[hips][3]).y;
        for (int p = 0; p < 4; ++p) {
            if (point_joint[p] != SKELETON_NO_JOINT) floor = std::min(floor, vec3(rest[point_joint[p]][3]).y);
        }
        hip_height = vec3(rest[hips][3]).y - floor;
    }
    const float scale = hip_height > 0.1f ? hip_height / REFERENCE_HIP_HEIGHT : 1.0f;
    const bool root_motion = r_clip.meta.has_root_motion();

    for (int s = 0; s < 2; ++s) {
        ClipFoot& out = r_clip.meta.feet[s];
        if (foot[s] == SKELETON_NO_JOINT) continue;
        out.joint = skeleton.names[foot[s]];
        if (toe[s] != SKELETON_NO_JOINT) out.toe = skeleton.names[toe[s]];
        std::vector<bool> raw(frames, false);
        for (int p = s * 2; p < s * 2 + 2; ++p) {
            if (point_joint[p] == SKELETON_NO_JOINT) continue;
            const std::vector<vec3>& pt = points[p];
            float low = pt[0].y;
            for (const vec3& v : pt) low = std::min(low, v.y);
            for (uint32_t f = 0; f < frames; ++f) {
                if (pt[f].y - low > CONTACT_HEIGHT * scale) continue;
                const uint32_t a = f > 0 ? f - 1 : f;
                const uint32_t b = f + 1 < frames ? f + 1 : f;
                const vec3 vel = b > a ? (pt[b] - pt[a]) * (r_clip.sample_rate / (float)(b - a)) : vec3(0.0f);
                const bool still = root_motion ? length(vel) < CONTACT_SPEED * scale : std::abs(vel.y) < CONTACT_VERTICAL_SPEED * scale;
                if (still) raw[f] = true;
            }
        }
        _contacts(raw, out.contacts);
    }
    if (!root_motion) {
        float total = 0.0f;
        uint32_t samples = 0;
        for (int s = 0; s < 2; ++s) {
            if (point_joint[s * 2] == SKELETON_NO_JOINT) continue;
            const std::vector<vec3>& pt = points[s * 2];
            for (const ClipContact& c : r_clip.meta.feet[s].contacts) {
                for (uint32_t f = c.start; f < c.end; ++f) {
                    total += length(vec2(pt[f + 1].x - pt[f].x, pt[f + 1].z - pt[f].z)) * r_clip.sample_rate;
                    samples++;
                }
            }
        }
        r_clip.meta.speed = samples ? total / (float)samples : 0.0f;
    }
    _phase(r_clip.meta, frames, r_clip.meta.phase);
}

}
