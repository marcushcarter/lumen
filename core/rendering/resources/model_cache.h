#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace lumen {

struct GeometryPool;

struct ModelCache
{
    static constexpr uint32_t INVALID_MESH = UINT32_MAX;

    struct Entry
    {
        Guid mesh;
        uint32_t mesh_index = INVALID_MESH;
    };

    std::unordered_map<Guid, Entry, GuidHash, GuidEq> by_guid;
    uint64_t version = 0;
    uint64_t _geometry_version = UINT64_MAX;

    Error load(const std::filesystem::path& p_path);
    void set(Guid p_guid, Guid p_mesh);
    void unload(Guid p_guid);
    void clear();
    void resolve(const GeometryPool& p_geometry);

    const Entry* get(Guid p_guid) const {
        auto it = by_guid.find(p_guid);
        return it == by_guid.end() ? nullptr : &it->second;
    }

    uint32_t mesh_index(Guid p_guid) const {
        const Entry* e = get(p_guid);
        return e ? e->mesh_index : INVALID_MESH;
    }
};

}