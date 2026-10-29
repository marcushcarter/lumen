#pragma once
#include <core/base/error.h>
#include <filesystem>
#include <string>

namespace lumen {

struct Project;

struct ProjectExporter
{
    static constexpr const wchar_t* RUNTIME_RESOURCE = L"LUMEN_RUNTIME";

    static std::filesystem::path _setting_path(const std::string& p_setting, const std::filesystem::path& p_fallback);
    static std::filesystem::path _folder_name(const Project& p_project);
    static std::filesystem::path _exe_name(const Project& p_project);

    static Error run(const Project& p_project, const std::filesystem::path& p_destination, std::filesystem::path& r_out);
    static void run_with_dialog(Project& r_project);
};

}
