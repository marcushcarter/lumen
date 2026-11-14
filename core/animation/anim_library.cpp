#include <core/animation/anim_library.h>
#include <core/animation/clip_player.h>
#include <core/assets/asset_common.h>
#include <core/base/console.h>
#include <system_error>

namespace lumen {

std::filesystem::path AnimLibrary::cooked_path(const std::filesystem::path& p_content_dir, Guid p_guid, const char* p_extension)
{
    char buf[Guid::PATH_BUFFER];
    p_guid.to_path_chars(buf);
    return p_content_dir / (std::string(buf) + p_extension);
}

void AnimLibrary::load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir)
{
    clear();
    content_dir = p_content_dir;
    assets_dir = p_assets_dir;
    _scan();
}

void AnimLibrary::clear()
{
    content_dir.clear();
    assets_dir.clear();
    asset_paths.clear();
    clips.clear();
    spaces.clear();
    locomotions.clear();
    skeleton = nullptr;
    scanned = -1.0e9;
}

void AnimLibrary::_scan()
{
    scanned = time;
    if (assets_dir.empty()) return;
    asset_paths.clear();
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(assets_dir, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext != SPACE_EXTENSION && ext != LLocomotion::EXTENSION && ext != LLocomotion::LEGACY_EXTENSION) continue;
        const AssetInfo info = read_asset_info(it->path());
        if (info.valid() && info.guid.valid()) asset_paths[info.guid] = it->path();
    }
}

template<typename T>
const T* AnimLibrary::_cached(std::unordered_map<Guid, Cached<T>>& r_cache, Guid p_guid, const char* p_extension, const char* p_kind)
{
    if (!p_guid.valid()) return nullptr;
    Cached<T>& c = r_cache[p_guid];
    if (c.path.empty()) {
        if (assets_dir.empty()) {
            c.path = cooked_path(content_dir, p_guid, p_extension);
        } else {
            auto it = asset_paths.find(p_guid);
            if (it == asset_paths.end() && time - scanned >= POLL_S) {
                _scan();
                it = asset_paths.find(p_guid);
            }
            if (it == asset_paths.end()) return nullptr;
            c.path = it->second;
        }
    }
    if (time - c.poll >= POLL_S) {
        c.poll = time;
        std::error_code ec;
        const std::filesystem::file_time_type t = std::filesystem::last_write_time(c.path, ec);
        if (ec) {
            c.path.clear();
            c.failed = true;
        } else if (t != c.time) {
            c.time = t;
            c.failed = c.asset.load(c.path) != Error::OK;
            if (c.failed) log_write("Animation: failed to load %s '%s'.", p_kind, c.path.string().c_str());
        }
    }
    return c.failed || c.time == std::filesystem::file_time_type{} ? nullptr : &c.asset;
}

const LBlendSpace* AnimLibrary::blend_space(Guid p_guid)
{
    return _cached(spaces, p_guid, SPACE_EXTENSION, "blend space");
}

const LLocomotion* AnimLibrary::locomotion(Guid p_guid)
{
    return _cached(locomotions, p_guid, LLocomotion::EXTENSION, "locomotion");
}

BoundClip AnimLibrary::clip(Guid p_guid)
{
    if (!p_guid.valid() || !skeleton || content_dir.empty()) return {};
    Clip& c = clips[p_guid];
    if (!c.failed && c.clip.frame_count == 0) {
        char buf[Guid::PATH_BUFFER];
        p_guid.to_path_chars(buf);
        c.failed = c.clip.load_cooked(content_dir / (std::string(buf) + ".bin")) != Error::OK || c.clip.frame_count == 0;
        if (c.failed) log_write("Animation: failed to load clip %s.", p_guid.to_string().c_str());
    }
    if (c.failed) return {};
    auto [it, added] = c.maps.try_emplace(skeleton->guid);
    if (added) clip_track_map(*skeleton, c.clip, it->second);
    return BoundClip{ &c.clip, &it->second };
}

}
