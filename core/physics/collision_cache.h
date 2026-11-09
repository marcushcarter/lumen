#pragma once
#include <core/assets/guid.h>
#include <core/assets/lcollision.h>
#include <filesystem>
#include <memory>

namespace lumen {

struct CollisionCache
{
    struct Impl;
    std::unique_ptr<Impl> impl;
    uint64_t version = 0;

    CollisionCache();
    ~CollisionCache();
    CollisionCache(const CollisionCache&) = delete;
    CollisionCache& operator=(const CollisionCache&) = delete;

    bool load(Guid p_mesh, const std::filesystem::path& p_collision_file);
    bool add(const LCollision& p_collision);
    void unload(Guid p_guid);
    void clear();
    bool has(Guid p_guid) const;
    bool find_type(Guid p_guid, CollisionType& r_type) const;
};

}
