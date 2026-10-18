#include <core/rendering/resources/model_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/lmodel.h>

namespace lumen {

Error ModelCache::load(const std::filesystem::path& p_path)
{
    using enum Error;
    LModel model;
    if (model.load(p_path) != OK || model.guid == Guid{}) return FAILED;
    by_guid[model.guid] = Entry{ model.mesh, INVALID_MESH };
    _geometry_version = UINT64_MAX;
    version++;
    return OK;
}

void ModelCache::set(Guid p_guid, Guid p_mesh)
{
    by_guid[p_guid] = Entry{ p_mesh, INVALID_MESH };
    _geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::unload(Guid p_guid)
{
    if (by_guid.erase(p_guid)) version++;
}

void ModelCache::clear()
{
    by_guid.clear();
    _geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::resolve(const GeometryPool& p_geometry)
{
    if (_geometry_version == p_geometry.version) return;
    for (auto& [guid, entry] : by_guid) entry.mesh_index = p_geometry.find(entry.mesh);
    _geometry_version = p_geometry.version;
    version++;
}

}