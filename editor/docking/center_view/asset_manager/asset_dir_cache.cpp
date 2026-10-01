#include <editor/docking/center_view/asset_manager/asset_dir_cache.h>
#include <algorithm>
#include <cctype>

namespace lumen {

void AssetDirCache::tick(double p_now)
{
    if (!refresh_requested && p_now - last_refresh < REFRESH_INTERVAL_S) return;
    dirs.clear();
    last_refresh = p_now;
    refresh_requested = false;
}

AssetDirCache::Dir& AssetDirCache::get(const std::filesystem::path& p_dir)
{
    auto [it, inserted] = dirs.try_emplace(p_dir);
    if (inserted) _scan(p_dir, it->second);
    return it->second;
}

void AssetDirCache::_scan(const std::filesystem::path& p_dir, Dir& r_dir)
{
    r_dir.path_str = p_dir.string();
    r_dir.name = p_dir.filename().string();

    std::error_code ec;
    r_dir.exists = std::filesystem::is_directory(p_dir, ec);
    if (!r_dir.exists) return;

    for (auto it = std::filesystem::directory_iterator(p_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        Entry e;
        e.path = it->path();
        e.is_dir = it->is_directory(ec);
        e.name = e.path.stem().string();
        e.type = e.path.extension().string();
        for (char& c : e.type) c = (char)std::toupper((unsigned char)c);
        e.lower_name = e.path.filename().string();
        for (char& c : e.lower_name) c = (char)std::tolower((unsigned char)c);
        e.is_texture = !e.is_dir && e.path.extension() == ".ltexture";
        if (e.is_dir) r_dir.subdirs.push_back(e.path);
        r_dir.entries.push_back(std::move(e));
    }

    std::sort(r_dir.subdirs.begin(), r_dir.subdirs.end());
    std::sort(r_dir.entries.begin(), r_dir.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.lower_name < b.lower_name;
    });
}

}