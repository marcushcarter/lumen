#pragma once
#include <core/assets/guid.h>
#include <core/assets/lskeleton.h>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct SkeletonCache
{
    struct Entry {
        LSkeleton skeleton;
        std::vector<mat4> rest_global;
    };

    std::unordered_map<Guid, Entry> entries;
    uint64_t version = 0;

    bool load(Guid p_guid, const std::filesystem::path& p_path);
    void unload(Guid p_guid);
    void clear();

    const Entry* get(Guid p_guid) const {
        auto it = entries.find(p_guid);
        return it == entries.end() ? nullptr : &it->second;
    }
};

}
