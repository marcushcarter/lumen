#pragma once
#include <core/base/error.h>
#include <core/io/image_io.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

struct Project;

struct IconCooker
{
    static constexpr uint32_t SIZES[] = { 256, 64, 48, 40, 32, 24, 20, 16 };
    static constexpr uint32_t MIN_SOURCE = 256;

    static ImageData<uint8_t, 4> _square(const ImageData<uint8_t, 4>& p_src);

    static Error cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst);
    static bool stale(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst);
    static Error cook_icon(const Project& p_project);
};

}
