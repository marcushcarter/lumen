#pragma once
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

struct Project;

struct SplashCooker
{
    static constexpr uint32_t WIDTH = 1920;
    static constexpr uint32_t HEIGHT = 1080;

    static Error cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst);
    static Error cook_splash(const Project& p_project);
};

}
