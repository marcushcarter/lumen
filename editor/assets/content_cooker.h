#pragma once
#include <core/assets/asset_common.h>
#include <filesystem>
#include <cstdint>

namespace lumen {

struct Project;

struct ContentCooker
{
    struct Result {
        uint32_t cooked = 0;
        uint32_t current = 0;
        uint32_t failed = 0;
    };

    static bool _stale(const std::filesystem::path& p_source, const std::filesystem::path& p_cooked, AssetType p_type);
    static void _cook_file(const Project& p_project, const std::filesystem::path& p_source, AssetType p_type, Result& r_result);

    static Result cook(const Project& p_project);
    static Result cook_root(const std::filesystem::path& p_root);
};

}
