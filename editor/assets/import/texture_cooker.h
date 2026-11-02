#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <core/assets/ltexture.h>
#include <core/io/image_io.h>
#include <filesystem>

namespace lumen {

struct Project;
struct EditorContext;

TextureUsage texture_usage_guess(const std::filesystem::path& p_src);

struct TextureCooker
{
    static constexpr float LOAD_SHARE = 0.08f;
    static constexpr float MIPS_SHARE = 0.15f;
    static constexpr float ENCODE_END = 0.97f;
    static constexpr uint32_t ENCODE_STRIP_ROWS = 128;

    struct CookSettings {
        TextureUsage usage = TextureUsage::COLOR;
        TextureQuality quality = TextureQuality::BEST;
        bool generate_mips = true;
        uint32_t max_size = 0;
        bool flip_green = false;
        float alpha_cutoff = 0.0f;
        float rdo_lambda = 0.0f;
        int32_t source_channel = -1;
    };

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_ltexture;
        std::filesystem::path content_bin;
        Guid guid;
        CookSettings settings;
        ImportStep step;
    };

    static Error _cook(const Job& p_job);
    static uint64_t cost(const std::filesystem::path& p_src, const CookSettings& p_settings);

    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const CookSettings& p_settings = {});

    // Reads a cooked texture back to straight RGBA8, from the smallest mip that's still at least
    // p_min_side on its long edge (mip 0 if none is). r_srgb tells whether the pixels are sRGB.
    static bool decode(const std::filesystem::path& p_content_bin, ImageData<uint8_t, 4>& r_image, bool& r_srgb, uint32_t p_min_side = UINT32_MAX);
};

}
