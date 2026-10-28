#pragma once
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace lumen {

struct Project;

struct IconCooker
{
    static constexpr uint32_t SIZES[] = { 256, 64, 48, 40, 32, 24, 20, 16 };
    static constexpr uint32_t MIN_SOURCE = 256;

    struct Image {
        std::vector<float> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        bool srgb = false;
    };

    static bool _decode(const std::filesystem::path& p_texture_bin, Image& r_image);
    static Image _square(const Image& p_src);
    static Image _resample(const Image& p_src, uint32_t p_width, uint32_t p_height);
    static bool _encode_png(const Image& p_image, std::vector<uint8_t>& r_png);

    static Error cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst);
    static bool stale(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst);
    static Error cook_window_icon(const Project& p_project);
};

}
