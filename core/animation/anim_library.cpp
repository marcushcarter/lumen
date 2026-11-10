#include <core/animation/anim_library.h>
#include <core/animation/clip_player.h>
#include <core/base/console.h>
#include <system_error>

namespace lumen {

std::filesystem::path AnimLibrary::cooked_space_path(const std::filesystem::path& p_content_dir, Guid p_guid)
{
    char buf[Guid::PATH_BUFFER];
    p_guid.to_path_chars(buf);
    return p_content_dir / (std::string(buf) + SPACE_EXTENSION);
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
    space_paths.clear();
    clips.clear();
    spaces.clear();
    skeleton = nullptr;
    scanned = -1.0e9;
}

void AnimLibrary::_scan()
{
    scanned = time;
    if (assets_dir.empty()) return;
    space_paths.clear();
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(assets_dir, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != SPACE_EXTENSION) continue;
        LBlendSpace space;
        if (space.load(it->path()) == Error::OK && space.guid.valid()) space_paths[space.guid] = it->path();
    }
}

const LBlendSpace* AnimLibrary::blend_space(Guid p_guid)
{
    if (!p_guid.valid()) return nullptr;
    Space& s = spaces[p_guid];
    if (s.path.empty()) {
        if (assets_dir.empty()) {
            s.path = cooked_space_path(content_dir, p_guid);
        } else {
            auto it = space_paths.find(p_guid);
            if (it == space_paths.end() && time - scanned >= POLL_S) {
                _scan();
                it = space_paths.find(p_guid);
            }
            if (it == space_paths.end()) return nullptr;
            s.path = it->second;
        }
    }
    if (time - s.poll >= POLL_S) {
        s.poll = time;
        std::error_code ec;
        const std::filesystem::file_time_type t = std::filesystem::last_write_time(s.path, ec);
        if (ec) {
            s.path.clear();
            s.failed = true;
        } else if (t != s.time) {
            s.time = t;
            s.failed = s.space.load(s.path) != Error::OK;
            if (s.failed) log_write("Animation: failed to load blend space '%s'.", s.path.string().c_str());
        }
    }
    return s.failed || s.time == std::filesystem::file_time_type{} ? nullptr : &s.space;
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
