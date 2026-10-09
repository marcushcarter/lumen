#include <editor/assets/asset_registry.h>
#include <core/project/project.h>

namespace lumen {

void AssetRegistry::_rebuild()
{
    dirty = false;
    entries.clear();
    if (root.empty()) return;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& path = it->path();
        const std::filesystem::path ext = path.extension();
        if (ext != ".lmesh" && ext != ".ltexture") continue;
        const AssetInfo info = read_asset_info(path);
        if (!info.valid()) continue;
        entries[info.guid] = Entry{ path, Project::asset_path_from(root, path), path.stem().string(), info.type };
    }
}

}