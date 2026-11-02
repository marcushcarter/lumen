#include <editor/assets/import/icon_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

namespace lumen {

ImageData<uint8_t, 4> IconCooker::_square(const ImageData<uint8_t, 4>& p_src)
{
    const int side = (std::max)(p_src.width, p_src.height);
    ImageData<uint8_t, 4> out = ImageIO::create<uint8_t, 4>(side, side);
    if (!out.valid()) return out;
    const int ox = (side - p_src.width) / 2;
    const int oy = (side - p_src.height) / 2;
    for (int y = 0; y < p_src.height; y++) {
        std::memcpy(out.pixels + ((size_t)(y + oy) * side + ox) * 4, p_src.pixels + (size_t)y * p_src.width * 4, (size_t)p_src.width * 4);
    }
    return out;
}

Error IconCooker::cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst)
{
    using enum Error;
    ImageData<uint8_t, 4> source;
    bool srgb = false;
    if (!TextureCooker::decode(p_texture_bin, source, srgb, MIN_SOURCE)) {
        log_write("IconCooker: '%s' isn't a cooked texture the icon cooker can read.", p_texture_bin.string().c_str());
        return FAILED;
    }
    ImageData<uint8_t, 4> square = _square(source);
    ImageIO::free_image(source);
    if (!square.valid()) return FAILED;

    std::vector<std::vector<uint8_t>> pngs;
    for (const uint32_t size : SIZES) {
        ImageData<uint8_t, 4> scaled = ImageIO::resize_rgba(square, (int)size, (int)size, srgb);
        std::vector<uint8_t> png;
        const bool encoded = ImageIO::encode_png(scaled, png);
        ImageIO::free_image(scaled);
        if (!encoded) {
            ImageIO::free_image(square);
            return FAILED;
        }
        pngs.push_back(std::move(png));
    }
    ImageIO::free_image(square);

    std::vector<uint8_t> ico;
    auto u8 = [&](uint32_t v) { ico.push_back((uint8_t)v); };
    auto u16 = [&](uint32_t v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    const uint32_t count = (uint32_t)std::size(SIZES);
    u16(0);
    u16(1);
    u16(count);
    uint32_t offset = 6 + 16 * count;
    for (uint32_t i = 0; i < count; i++) {
        u8(SIZES[i] >= 256 ? 0 : SIZES[i]);
        u8(SIZES[i] >= 256 ? 0 : SIZES[i]);
        u8(0);
        u8(0);
        u16(1);
        u16(32);
        u32((uint32_t)pngs[i].size());
        u32(offset);
        offset += (uint32_t)pngs[i].size();
    }
    for (const std::vector<uint8_t>& png : pngs) ico.insert(ico.end(), png.begin(), png.end());

    std::error_code ec;
    std::filesystem::create_directories(p_dst.parent_path(), ec);
    if (!write_file_atomic(p_dst, ico.data(), ico.size())) {
        log_write("IconCooker: failed to write '%s'.", p_dst.string().c_str());
        return FAILED;
    }
    return OK;
}

bool IconCooker::stale(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst)
{
    std::error_code ec;
    if (!std::filesystem::exists(p_dst, ec)) return true;
    const auto src_time = std::filesystem::last_write_time(p_texture_bin, ec);
    if (ec) return false;
    const auto dst_time = std::filesystem::last_write_time(p_dst, ec);
    return ec || src_time > dst_time;
}

Error IconCooker::cook_icon(const Project& p_project)
{
    using enum Error;
    const std::filesystem::path dst = p_project.icon_path();
    std::error_code ec;
    if (!p_project.settings.window_icon.valid()) {
        std::filesystem::remove(dst, ec);
        return OK;
    }
    return cook(p_project.content_path(p_project.settings.window_icon), dst);
}

}
