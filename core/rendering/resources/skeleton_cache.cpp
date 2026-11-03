#include <core/rendering/resources/skeleton_cache.h>
#include <core/base/error.h>

namespace lumen {

bool SkeletonCache::load(Guid p_guid, const std::filesystem::path& p_path)
{
    Entry e;
    if (e.skeleton.load_cooked(p_path) != Error::OK || e.skeleton.guid != p_guid) {
        log_write("SkeletonCache: failed to load '%s'.", p_path.string().c_str());
        return false;
    }
    e.rest_global.resize(e.skeleton.joint_count());
    e.skeleton.globals(e.skeleton.rest.data(), e.rest_global.data());
    entries.insert_or_assign(p_guid, std::move(e));
    version++;
    return true;
}

void SkeletonCache::unload(Guid p_guid)
{
    if (entries.erase(p_guid)) version++;
}

void SkeletonCache::clear()
{
    entries.clear();
    version++;
}

}
