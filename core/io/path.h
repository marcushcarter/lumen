#pragma once
#include <core/base/error.h>
#include <filesystem>
#include <string_view>

namespace lumen {

struct Paths
{
    static std::filesystem::path local_data(std::wstring_view p_subpath = {});
    static std::filesystem::path local_low_data(std::wstring_view p_subpath = {});
    static std::filesystem::path roaming_data(std::wstring_view p_subpath = {});

    static std::filesystem::path shader_cache();
    static std::filesystem::path pipeline_cache();

    static std::filesystem::path screenshots();
    static std::filesystem::path logs();

    static std::filesystem::path executable_dir();

    static Error set_hidden(const std::filesystem::path& p_path, bool p_hidden = true);
    static void reveal_in_explorer(const std::filesystem::path& p_path);

    static void move(const std::filesystem::path& p_src, const std::filesystem::path& p_dst);
    static void rename(const std::filesystem::path& p_path, std::string_view p_new_stem);
    static void remove_to_recycle(const std::filesystem::path& p_src);

    static bool is_under(const std::filesystem::path& p_path, const std::filesystem::path& p_base);
    static bool has_subdir(const std::filesystem::path& p_path);
    static void gather_subdirs(const std::filesystem::path& p_path, std::vector<std::filesystem::path>& p_out);
    
};
    
};