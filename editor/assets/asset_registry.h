#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace lumen {

struct AssetRegistry
{
    struct Entry
    {
        std::filesystem::path path;
        std::string asset_path;
        std::string name;
        AssetType type = AssetType::NONE;
    };

    std::unordered_map<Guid, Entry> entries;
    std::filesystem::path root;
    bool dirty = false;

    void open(const std::filesystem::path& p_root) { root = p_root; dirty = true; }
    void close() { entries.clear(); root.clear(); dirty = false; }
    void request_rebuild() { dirty = true; }
    void tick() { if (dirty) _rebuild(); }

    const Entry* find(Guid p_guid) const {
        auto it = entries.find(p_guid);
        return it == entries.end() ? nullptr : &it->second;
    }

    void _rebuild();
};

}