#include <core/assets/lanim.h>
#include <core/assets/asset_common.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lumen {

inline constexpr uint32_t CLIP_META_MAGIC = uint32_t('C') | (uint32_t('M') << 8) | (uint32_t('E') << 16) | (uint32_t('T') << 24);
inline constexpr uint32_t CLIP_MAX_STRING = 4096;
inline constexpr uint32_t CLIP_MAX_EVENTS = 4096;
inline constexpr uint32_t CLIP_MAX_CURVES = 256;
inline constexpr uint32_t CLIP_MAX_KEYS = 65536;

const char* clip_role_name(ClipRole p_role)
{
    switch (p_role) {
        case ClipRole::IDLE: return "Idle";
        case ClipRole::CYCLE: return "Cycle";
        case ClipRole::START: return "Start";
        case ClipRole::STOP: return "Stop";
        case ClipRole::PIVOT: return "Pivot";
        case ClipRole::TURN: return "Turn";
        case ClipRole::JUMP: return "Jump";
        case ClipRole::LAND: return "Land";
        default: return "None";
    }
}

const char* clip_gait_name(ClipGait p_gait)
{
    switch (p_gait) {
        case ClipGait::WALK: return "Walk";
        case ClipGait::JOG: return "Jog";
        case ClipGait::RUN: return "Run";
        case ClipGait::SPRINT: return "Sprint";
        case ClipGait::CROUCH: return "Crouch";
        default: return "None";
    }
}

const char* clip_foot_name(ClipFootSide p_side)
{
    return p_side == ClipFootSide::LEFT ? "Left" : "Right";
}

bool ClipFoot::planted(uint32_t p_frame) const
{
    for (const ClipContact& c : contacts) {
        if (p_frame >= c.start && p_frame <= c.end) return true;
    }
    return false;
}

float ClipCurve::evaluate(float p_time) const
{
    if (keys.empty()) return 0.0f;
    if (p_time <= keys.front().x) return keys.front().y;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (p_time > keys[i].x) continue;
        const float span = keys[i].x - keys[i - 1].x;
        const float t = span > 1e-6f ? (p_time - keys[i - 1].x) / span : 1.0f;
        return mix(keys[i - 1].y, keys[i].y, t);
    }
    return keys.back().y;
}

uint32_t LAnimation::frame_at(float p_time) const
{
    if (frame_count == 0) return 0;
    return std::min((uint32_t)std::lround(std::clamp(p_time, 0.0f, duration) * sample_rate), frame_count - 1);
}

template <class T>
static T _curve_at(const std::vector<T>& p_values, float p_time, float p_duration, float p_rate)
{
    const uint32_t count = (uint32_t)p_values.size();
    const float f = std::clamp(p_time, 0.0f, p_duration) * p_rate;
    const uint32_t i0 = std::min((uint32_t)f, count - 1);
    const uint32_t i1 = std::min(i0 + 1, count - 1);
    return mix(p_values[i0], p_values[i1], f - (float)i0);
}

float LAnimation::phase_at(float p_time) const
{
    if (meta.phase.empty()) return 0.0f;
    const uint32_t count = (uint32_t)meta.phase.size();
    const float f = std::clamp(p_time, 0.0f, duration) * sample_rate;
    const uint32_t i0 = std::min((uint32_t)f, count - 1);
    const uint32_t i1 = std::min(i0 + 1, count - 1);
    const float a = meta.phase[i0];
    float b = meta.phase[i1];
    if (b < a - 0.5f) b += 1.0f;
    const float p = mix(a, b, f - (float)i0);
    return p - std::floor(p);
}

float LAnimation::traveled_at(float p_time) const
{
    return meta.traveled.empty() ? 0.0f : _curve_at(meta.traveled, p_time, duration, sample_rate);
}

vec4 LAnimation::trajectory_at(float p_time) const
{
    return meta.trajectory.empty() ? vec4(0.0f) : _curve_at(meta.trajectory, p_time, duration, sample_rate);
}

struct _MetaWriter
{
    std::vector<uint8_t>& out;

    template <class T>
    void put(const T& p_value)
    {
        const size_t at = out.size();
        out.resize(at + sizeof(T));
        std::memcpy(out.data() + at, &p_value, sizeof(T));
    }
    void str(const std::string& p_text)
    {
        put((uint32_t)p_text.size());
        out.insert(out.end(), p_text.begin(), p_text.end());
    }
    template <class T>
    void list(const std::vector<T>& p_values)
    {
        put((uint32_t)p_values.size());
        const size_t at = out.size();
        out.resize(at + p_values.size() * sizeof(T));
        if (!p_values.empty()) std::memcpy(out.data() + at, p_values.data(), p_values.size() * sizeof(T));
    }
};

struct _MetaReader
{
    const uint8_t* at;
    size_t left;

    template <class T>
    bool get(T& r_value)
    {
        if (left < sizeof(T)) return false;
        std::memcpy(&r_value, at, sizeof(T));
        at += sizeof(T);
        left -= sizeof(T);
        return true;
    }
    bool str(std::string& r_text)
    {
        uint32_t len = 0;
        if (!get(len) || len > CLIP_MAX_STRING || left < len) return false;
        r_text.assign(reinterpret_cast<const char*>(at), len);
        at += len;
        left -= len;
        return true;
    }
    template <class T>
    bool list(std::vector<T>& r_values, uint32_t p_max)
    {
        uint32_t count = 0;
        if (!get(count) || count > p_max || left < (size_t)count * sizeof(T)) return false;
        r_values.resize(count);
        if (count) std::memcpy(r_values.data(), at, (size_t)count * sizeof(T));
        at += (size_t)count * sizeof(T);
        left -= (size_t)count * sizeof(T);
        return true;
    }
};

static void _write_meta(const LAnimation& p_clip, std::vector<uint8_t>& r_out)
{
    const ClipMeta& m = p_clip.meta;
    _MetaWriter w{ r_out };
    w.put(CLIP_META_MAGIC);
    w.put((uint8_t)m.role);
    w.put((uint8_t)m.gait);
    w.put((uint8_t)(m.has_direction ? 1 : 0));
    w.put((uint8_t)0);
    w.put(m.direction);
    w.str(m.root_joint);
    w.put(m.distance);
    w.put(m.speed);
    w.put(m.turn);
    w.put(m.move_direction);
    w.put(m.displacement);
    for (const ClipFoot& foot : m.feet) {
        w.str(foot.joint);
        w.str(foot.toe);
        w.list(foot.contacts);
    }
    w.list(m.phase);
    w.list(m.traveled);
    w.list(m.trajectory);
    w.put((uint32_t)p_clip.events.size());
    for (const ClipEvent& e : p_clip.events) {
        w.str(e.name);
        w.put(e.time);
    }
    w.put((uint32_t)p_clip.curves.size());
    for (const ClipCurve& c : p_clip.curves) {
        w.str(c.name);
        w.list(c.keys);
    }
}

static bool _read_meta(_MetaReader& r, LAnimation& r_clip)
{
    ClipMeta& m = r_clip.meta;
    uint32_t magic = 0;
    uint8_t role = 0, gait = 0, has_direction = 0, pad = 0;
    if (!r.get(magic) || magic != CLIP_META_MAGIC) return false;
    if (!r.get(role) || !r.get(gait) || !r.get(has_direction) || !r.get(pad)) return false;
    if (role >= (uint8_t)ClipRole::COUNT || gait >= (uint8_t)ClipGait::COUNT) return false;
    m.role = (ClipRole)role;
    m.gait = (ClipGait)gait;
    m.has_direction = has_direction != 0;
    if (!r.get(m.direction) || !r.str(m.root_joint)) return false;
    if (!r.get(m.distance) || !r.get(m.speed) || !r.get(m.turn) || !r.get(m.move_direction) || !r.get(m.displacement)) return false;
    for (ClipFoot& foot : m.feet) {
        if (!r.str(foot.joint) || !r.str(foot.toe) || !r.list(foot.contacts, r_clip.frame_count)) return false;
        for (const ClipContact& c : foot.contacts) {
            if (c.start > c.end || c.end >= r_clip.frame_count) return false;
        }
    }
    if (!r.list(m.phase, r_clip.frame_count) || !r.list(m.traveled, r_clip.frame_count) || !r.list(m.trajectory, r_clip.frame_count)) return false;
    if (!m.phase.empty() && m.phase.size() != r_clip.frame_count) return false;
    if (!m.traveled.empty() && m.traveled.size() != r_clip.frame_count) return false;
    if (!m.trajectory.empty() && m.trajectory.size() != r_clip.frame_count) return false;
    uint32_t count = 0;
    if (!r.get(count) || count > CLIP_MAX_EVENTS) return false;
    r_clip.events.resize(count);
    for (ClipEvent& e : r_clip.events) {
        if (!r.str(e.name) || !r.get(e.time)) return false;
    }
    if (!r.get(count) || count > CLIP_MAX_CURVES) return false;
    r_clip.curves.resize(count);
    for (ClipCurve& c : r_clip.curves) {
        if (!r.str(c.name) || !r.list(c.keys, CLIP_MAX_KEYS)) return false;
    }
    return r.left == 0;
}

JointTransform LAnimation::sample(uint32_t p_track, float p_time) const
{
    const JointTransform* frames = samples.data() + (size_t)p_track * frame_count;
    const float f = std::clamp(p_time, 0.0f, duration) * sample_rate;
    const uint32_t i0 = std::min((uint32_t)f, frame_count - 1);
    const uint32_t i1 = std::min(i0 + 1, frame_count - 1);
    const float t = f - (float)i0;
    const JointTransform& a = frames[i0];
    const JointTransform& b = frames[i1];
    JointTransform out;
    out.translation = mix(a.translation, b.translation, t);
    out.rotation = slerp(a.rotation, b.rotation, t);
    out.scale = mix(a.scale, b.scale, t);
    return out;
}

Error LAnimation::load_cooked(const std::filesystem::path& p_path)
{
    using enum Error;
    LAssetHeader header{};
    std::vector<uint8_t> payload;
    if (!read_cooked_asset(p_path, AssetType::ANIMATION, header, payload) || payload.size() < sizeof(LAnimPayloadHeader)) return FAILED;

    LAnimPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    if (ph.track_count > ANIM_MAX_TRACKS || ph.frame_count == 0 || ph.frame_count > ANIM_MAX_FRAMES || ph.sample_rate <= 0.0f) return FAILED;
    const size_t sample_bytes = (size_t)ph.track_count * ph.frame_count * sizeof(JointTransform);
    const size_t body_bytes = sizeof(ph) + ph.name_bytes + sample_bytes;
    if (payload.size() < body_bytes) return FAILED;

    *this = LAnimation{};
    guid = header.guid;
    skeleton = ph.skeleton;
    duration = ph.duration;
    sample_rate = ph.sample_rate;
    frame_count = ph.frame_count;

    const char* text = reinterpret_cast<const char*>(payload.data() + sizeof(ph));
    size_t at = 0;
    tracks.resize(ph.track_count);
    for (uint32_t i = 0; i < ph.track_count; ++i) {
        const size_t len = strnlen(text + at, ph.name_bytes - at);
        if (at + len >= ph.name_bytes) return FAILED;
        tracks[i].assign(text + at, len);
        at += len + 1;
    }
    samples.resize((size_t)ph.track_count * ph.frame_count);
    if (sample_bytes) std::memcpy(samples.data(), payload.data() + sizeof(ph) + ph.name_bytes, sample_bytes);
    _MetaReader reader{ payload.data() + body_bytes, payload.size() - body_bytes };
    if (!_read_meta(reader, *this)) {
        *this = LAnimation{};
        return FAILED;
    }
    return OK;
}

Error LAnimation::save_cooked(const std::filesystem::path& p_path) const
{
    using enum Error;
    const uint32_t count = track_count();
    if (count > ANIM_MAX_TRACKS || frame_count == 0 || frame_count > ANIM_MAX_FRAMES || samples.size() != (size_t)count * frame_count) return FAILED;

    std::string text;
    for (const std::string& n : tracks) {
        text.append(n);
        text.push_back('\0');
    }

    const LAnimPayloadHeader ph{ skeleton, duration, sample_rate, frame_count, count, (uint32_t)text.size(), 0 };
    const size_t sample_bytes = samples.size() * sizeof(JointTransform);
    std::vector<uint8_t> payload(sizeof(ph) + text.size() + sample_bytes);
    std::memcpy(payload.data(), &ph, sizeof(ph));
    if (!text.empty()) std::memcpy(payload.data() + sizeof(ph), text.data(), text.size());
    if (sample_bytes) std::memcpy(payload.data() + sizeof(ph) + text.size(), samples.data(), sample_bytes);
    _write_meta(*this, payload);
    return write_cooked_asset(p_path, AssetType::ANIMATION, guid, payload.data(), payload.size()) ? OK : FAILED;
}

}
