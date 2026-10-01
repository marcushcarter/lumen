#pragma once
#include <core/assets/guid.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct AssetDirCache
{
    static constexpr double REFRESH_INTERVAL_S = 1.0;

    struct Entry
    {
        std::filesystem::path path;
        std::string name;
        std::string type;
        std::string lower_name;
        Guid guid;
        bool is_dir = false;
        bool is_texture = false;
        bool guid_checked = false;
    };

    struct Dir
    {
        std::string path_str;
        std::string name;
        std::vector<std::filesystem::path> subdirs;
        std::vector<Entry> entries;
        bool exists = false;
    };

    std::unordered_map<std::filesystem::path, Dir> dirs;
    double last_refresh = -1.0e9;
    bool refresh_requested = false;

    void tick(double p_now);
    void request_refresh() { refresh_requested = true; }
    Dir& get(const std::filesystem::path& p_dir);

    void _scan(const std::filesystem::path& p_dir, Dir& r_dir);
};

}