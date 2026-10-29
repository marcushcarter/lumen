#pragma once
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lumen {

struct Project;

struct ProjectExporter
{
    static constexpr const wchar_t* RUNTIME_RESOURCE = L"LUMEN_RUNTIME";
    static constexpr uint32_t ICON_GROUP_ID = 1;
    static constexpr uint32_t ICON_UPDATE_ATTEMPTS = 3;
    static constexpr uint32_t ICON_RETRY_MS = 100;

    struct IcoHeader {
        uint16_t reserved;
        uint16_t type;
        uint16_t count;
    };

    struct IcoEntry {
        uint8_t width;
        uint8_t height;
        uint8_t colors;
        uint8_t reserved;
        uint16_t planes;
        uint16_t bit_count;
        uint32_t bytes;
        uint32_t offset;
    };

#pragma pack(push, 2)
    struct GroupIconEntry {
        uint8_t width;
        uint8_t height;
        uint8_t colors;
        uint8_t reserved;
        uint16_t planes;
        uint16_t bit_count;
        uint32_t bytes;
        uint16_t id;
    };
#pragma pack(pop)

    static std::filesystem::path _setting_path(const std::string& p_setting, const std::filesystem::path& p_fallback);
    static std::filesystem::path _folder_name(const Project& p_project);
    static std::filesystem::path _exe_name(const Project& p_project);
    static bool _update_icon_resources(const std::filesystem::path& p_exe, const std::vector<uint8_t>& p_ico);
    static bool _set_exe_icon(const std::filesystem::path& p_exe, const std::filesystem::path& p_icon);

    static Error run(const Project& p_project, const std::filesystem::path& p_destination, std::filesystem::path& r_out);
    static void run_with_dialog(Project& r_project);
};

}
