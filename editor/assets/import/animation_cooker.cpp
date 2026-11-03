#include <editor/assets/import/animation_cooker.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lumen {

static mat4 _to_glm(const aiMatrix4x4& m)
{
    return mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

static JointTransform _decompose(const mat4& p_m)
{
    JointTransform t;
    t.translation = vec3(p_m[3]);
    t.scale = vec3(length(vec3(p_m[0])), length(vec3(p_m[1])), length(vec3(p_m[2])));
    if (determinant(mat3(p_m)) < 0.0f) t.scale.x = -t.scale.x;
    mat3 rot(1.0f);
    for (int c = 0; c < 3; c++) {
        if (t.scale[c] != 0.0f) rot[c] = vec3(p_m[c]) / t.scale[c];
    }
    t.rotation = normalize(quat_cast(rot));
    return t;
}

template <class Key>
static uint32_t _key_before(const Key* p_keys, uint32_t p_count, double p_time)
{
    uint32_t i = 0;
    while (i + 1 < p_count && p_keys[i + 1].mTime <= p_time) i++;
    return i;
}

static vec3 _vector_at(const aiVectorKey* p_keys, uint32_t p_count, double p_time, vec3 p_fallback)
{
    if (p_count == 0) return p_fallback;
    const uint32_t i = _key_before(p_keys, p_count, p_time);
    const aiVector3D& a = p_keys[i].mValue;
    if (i + 1 >= p_count || p_time <= p_keys[i].mTime) return vec3(a.x, a.y, a.z);
    const aiVector3D& b = p_keys[i + 1].mValue;
    const float t = (float)((p_time - p_keys[i].mTime) / (p_keys[i + 1].mTime - p_keys[i].mTime));
    return mix(vec3(a.x, a.y, a.z), vec3(b.x, b.y, b.z), t);
}

static quat _rotation_at(const aiQuatKey* p_keys, uint32_t p_count, double p_time, quat p_fallback)
{
    if (p_count == 0) return p_fallback;
    const uint32_t i = _key_before(p_keys, p_count, p_time);
    const aiQuaternion& a = p_keys[i].mValue;
    if (i + 1 >= p_count || p_time <= p_keys[i].mTime) return normalize(quat(a.w, a.x, a.y, a.z));
    const aiQuaternion& b = p_keys[i + 1].mValue;
    const float t = (float)((p_time - p_keys[i].mTime) / (p_keys[i + 1].mTime - p_keys[i].mTime));
    return normalize(slerp(quat(a.w, a.x, a.y, a.z), quat(b.w, b.x, b.y, b.z), t));
}

bool AnimationCooker::build(const aiScene* p_scene, const aiAnimation* p_anim, const MeshCooker::CookSettings& p_settings, const LSkeleton* p_skeleton, LAnimation& r_clip)
{
    r_clip = {};
    if (!p_scene || !p_scene->mRootNode || !p_anim || p_anim->mNumChannels == 0) return false;

    const double ticks = p_anim->mTicksPerSecond > 0.0 ? p_anim->mTicksPerSecond : 25.0;
    r_clip.duration = (float)std::max(p_anim->mDuration / ticks, 0.0);
    r_clip.sample_rate = SAMPLE_RATE;
    r_clip.frame_count = std::min((uint32_t)std::ceil(r_clip.duration * SAMPLE_RATE - 1.0e-3f) + 1u, ANIM_MAX_FRAMES);

    std::unordered_map<std::string, const aiNodeAnim*> channels;
    for (uint32_t c = 0; c < p_anim->mNumChannels; c++) channels.emplace(p_anim->mChannels[c]->mNodeName.C_Str(), p_anim->mChannels[c]);

    std::unordered_set<std::string> joints;
    if (p_skeleton) joints.insert(p_skeleton->names.begin(), p_skeleton->names.end());
    else for (const auto& [name, channel] : channels) joints.insert(name);

    auto is_top = [&](const aiNode* p_node) {
        for (const aiNode* n = p_node->mParent; n; n = n->mParent) {
            if (joints.contains(n->mName.C_Str())) return false;
        }
        return true;
    };
    auto local_at = [&](const aiNode* p_node, double p_time) {
        const JointTransform rest = _decompose(_to_glm(p_node->mTransformation));
        auto it = channels.find(p_node->mName.C_Str());
        if (it == channels.end()) return rest;
        const aiNodeAnim* ch = it->second;
        JointTransform t;
        t.translation = _vector_at(ch->mPositionKeys, ch->mNumPositionKeys, p_time, rest.translation);
        t.rotation = _rotation_at(ch->mRotationKeys, ch->mNumRotationKeys, p_time, rest.rotation);
        t.scale = _vector_at(ch->mScalingKeys, ch->mNumScalingKeys, p_time, rest.scale);
        return t;
    };

    std::vector<const aiNode*> targets;
    std::unordered_set<const aiNode*> added;
    for (uint32_t c = 0; c < p_anim->mNumChannels; c++) {
        const aiNode* node = p_scene->mRootNode->FindNode(p_anim->mChannels[c]->mNodeName);
        if (node && joints.contains(node->mName.C_Str()) && added.insert(node).second) targets.push_back(node);
    }
    for (const std::string& name : joints) {
        const aiNode* node = p_scene->mRootNode->FindNode(name.c_str());
        if (!node || added.contains(node) || !is_top(node)) continue;
        bool animated = false;
        for (const aiNode* n = node->mParent; n && !animated; n = n->mParent) animated = channels.contains(n->mName.C_Str());
        if (animated && added.insert(node).second) targets.push_back(node);
    }

    const mat4 import = translate(mat4(1.0f), p_settings.translation) * mat4_cast(quat(radians(p_settings.rotation))) * glm::scale(mat4(1.0f), vec3(p_settings.scale));
    for (const aiNode* node : targets) {
        if (r_clip.tracks.size() >= ANIM_MAX_TRACKS) break;
        const bool top = is_top(node);
        r_clip.tracks.push_back(node->mName.C_Str());
        for (uint32_t f = 0; f < r_clip.frame_count; f++) {
            const double time = std::min((double)f / SAMPLE_RATE, (double)r_clip.duration) * ticks;
            if (!top) {
                r_clip.samples.push_back(local_at(node, time));
                continue;
            }
            mat4 world(1.0f);
            for (const aiNode* n = node; n; n = n->mParent) world = local_at(n, time).matrix() * world;
            r_clip.samples.push_back(_decompose(import * world));
        }
    }
    return !r_clip.tracks.empty();
}

Guid AnimationCooker::find_skeleton(const Project& p_project, const aiAnimation* p_anim, LSkeleton& r_skeleton)
{
    std::unordered_set<std::string> channels;
    for (uint32_t c = 0; c < p_anim->mNumChannels; c++) channels.insert(p_anim->mChannels[c]->mNodeName.C_Str());

    uint32_t best = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(p_project.assets_dir, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".lskeleton") continue;
        const AssetInfo info = read_asset_info(it->path());
        if (!info.valid() || info.type != AssetType::SKELETON) continue;
        LSkeleton skeleton;
        if (skeleton.load_cooked(p_project.content_path(info.guid)) != Error::OK) continue;
        uint32_t overlap = 0;
        for (const std::string& n : skeleton.names) overlap += channels.contains(n) ? 1u : 0u;
        if (overlap <= best || overlap * 2 < (uint32_t)channels.size()) continue;
        best = overlap;
        r_skeleton = std::move(skeleton);
    }
    return best ? r_skeleton.guid : Guid{};
}

Error AnimationCooker::cook(const Job& p_job, const LAnimation& p_clip)
{
    using enum Error;
    if (p_clip.save_cooked(p_job.content_bin) != OK) {
        log_write("AnimationCooker: failed to write '%s'.", p_job.content_bin.string().c_str());
        return FAILED;
    }

    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_clip.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::ANIMATION) } }},
        { "import", toml::table{
            { "source", p_job.source.generic_string() },
            { "clip", p_job.clip } }},
        { std::string(asset_type_section(AssetType::ANIMATION)), toml::table{
            { "skeleton", p_clip.skeleton.to_string() },
            { "loop", p_job.loop },
            { "duration", static_cast<double>(p_clip.duration) },
            { "frames", static_cast<int64_t>(p_clip.frame_count) },
            { "tracks", static_cast<int64_t>(p_clip.track_count()) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_job.dst_lanim, ss.str()) ? OK : FAILED;
}

}
