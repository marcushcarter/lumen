#include <core/assets/lanim.h>
#include <core/assets/asset_common.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lumen {

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
    if (payload.size() != sizeof(ph) + ph.name_bytes + sample_bytes) return FAILED;

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
    return write_cooked_asset(p_path, AssetType::ANIMATION, guid, payload.data(), payload.size()) ? OK : FAILED;
}

}
