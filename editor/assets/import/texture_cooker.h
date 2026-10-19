#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <core/assets/ltexture.h>
#include <filesystem>

namespace lumen {

struct Project;
struct EditorContext;

TextureUsage texture_usage_guess(const std::filesystem::path& p_src);

struct TextureCooker
{
    struct CookSettings {
        TextureUsage usage = TextureUsage::COLOR;
        // TextureQuality quality = TextureQuality::BEST;
        TextureQuality quality = TextureQuality::FAST;
        bool generate_mips = true;
        // uint32_t max_size = 0;
        uint32_t max_size = 1024;
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
        std::shared_ptr<ImportControl> progress;
    };
    
    static Error _cook(const Job& p_job);

    static void import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const CookSettings& p_settings = {});
};

}