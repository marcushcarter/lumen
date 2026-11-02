#include <editor/assets/import/splash_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <core/base/color.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <algorithm>
#include <vector>
#include <cmath>

namespace lumen {

Error SplashCooker::cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst)
{
    using enum Error;
    ImageData<uint8_t, 4> source;
    bool srgb = false;
    if (!TextureCooker::decode(p_texture_bin, source, srgb)) {
        log_write("SplashCooker: couldn't decode '%s'.", p_texture_bin.string().c_str());
        return FAILED;
    }

    // Scale to cover the splash, then center-crop.
    const float cover = (std::max)((float)WIDTH / source.width, (float)HEIGHT / source.height);
    const int scaled_w = (std::max)((int)WIDTH, (int)std::lround(source.width * cover));
    const int scaled_h = (std::max)((int)HEIGHT, (int)std::lround(source.height * cover));
    ImageData<uint8_t, 4> scaled = ImageIO::resize_rgba(source, scaled_w, scaled_h, srgb);
    ImageIO::free_image(source);
    if (!scaled.valid()) return FAILED;
    const int crop_x = (scaled.width - (int)WIDTH) / 2;
    const int crop_y = (scaled.height - (int)HEIGHT) / 2;

    // The window is opaque, so transparent pixels are composited over black.
    ImageData<uint8_t, 4> splash = ImageIO::create<uint8_t, 4>((int)WIDTH, (int)HEIGHT);
    if (!splash.valid()) {
        ImageIO::free_image(scaled);
        return FAILED;
    }
    for (size_t i = 0; i < (size_t)WIDTH * HEIGHT; i++) {
        const size_t x = crop_x + i % WIDTH;
        const size_t y = crop_y + i / WIDTH;
        const uint8_t* s = scaled.pixels + (y * scaled.width + x) * 4;
        const float a = s[3] / 255.0f;
        uint8_t* d = splash.pixels + i * 4;
        for (int k = 0; k < 3; k++) {
            const float v = s[k] / 255.0f;
            const float over = srgb ? color_linear_to_srgb(color_srgb_to_linear(v) * a) : v * a;
            d[k] = (uint8_t)std::lround(std::clamp(over, 0.0f, 1.0f) * 255.0f);
        }
        d[3] = 255;
    }
    ImageIO::free_image(scaled);

    std::vector<uint8_t> png;
    const bool encoded = ImageIO::encode_png(splash, png);
    ImageIO::free_image(splash);
    std::error_code ec;
    std::filesystem::create_directories(p_dst.parent_path(), ec);
    if (!encoded || !write_file_atomic(p_dst, png.data(), png.size())) {
        log_write("SplashCooker: failed to write '%s'.", p_dst.string().c_str());
        return FAILED;
    }
    return OK;
}

Error SplashCooker::cook_splash(const Project& p_project)
{
    using enum Error;
    const std::filesystem::path dst = p_project.splash_path();
    std::error_code ec;
    if (!p_project.settings.splash_image.valid()) {
        std::filesystem::remove(dst, ec);
        return OK;
    }
    return cook(p_project.content_path(p_project.settings.splash_image), dst);
}

}
