#include <editor/assets/import/icon_cooker.h>
#include <editor/assets/write_atomic.h>
#include <core/assets/asset_common.h>
#include <core/assets/ltexture.h>
#include <core/project/project.h>
#include <vulkan/vulkan.h>
#include <bc7decomp.h>
#include <rgbcx.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace lumen {

static float _srgb_to_linear(float p_c)
{
    return p_c <= 0.04045f ? p_c / 12.92f : std::pow((p_c + 0.055f) / 1.055f, 2.4f);
}

static float _linear_to_srgb(float p_c)
{
    return p_c <= 0.0031308f ? p_c * 12.92f : 1.055f * std::pow(p_c, 1.0f / 2.4f) - 0.055f;
}

static size_t _mip_bytes(uint32_t p_w, uint32_t p_h, uint32_t p_block_bytes)
{
    return (size_t)((p_w + 3) / 4) * ((p_h + 3) / 4) * p_block_bytes;
}

bool IconCooker::_decode(const std::filesystem::path& p_texture_bin, Image& r_image)
{
    std::ifstream in(p_texture_bin, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(LAssetHeader) + sizeof(LTexturePayloadHeader)) return false;

    LAssetHeader ah;
    LTexturePayloadHeader ph;
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    std::memcpy(&ph, bytes.data() + sizeof(ah), sizeof(ph));
    if (ah.magic != BCON_MAGIC || ah.type != AssetType::TEXTURE || ph.width == 0 || ph.height == 0) return false;

    const VkFormat format = (VkFormat)ph.vk_format;
    uint32_t block_bytes = 0;
    switch (format) {
        case VK_FORMAT_BC7_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK: block_bytes = 16; break;
        case VK_FORMAT_BC4_UNORM_BLOCK: block_bytes = 8; break;
        default: return false;
    }
    const bool srgb = format == VK_FORMAT_BC7_SRGB_BLOCK;

    size_t offset = sizeof(ah) + sizeof(ph);
    uint32_t w = ph.width;
    uint32_t h = ph.height;
    for (uint32_t mip = 0; mip + 1 < ph.mip_count; mip++) {
        const uint32_t nw = std::max(w / 2, 1u);
        const uint32_t nh = std::max(h / 2, 1u);
        if (std::max(nw, nh) < MIN_SOURCE) break;
        offset += _mip_bytes(w, h, block_bytes);
        w = nw;
        h = nh;
    }
    if (offset + _mip_bytes(w, h, block_bytes) > bytes.size()) return false;

    r_image.width = w;
    r_image.height = h;
    r_image.srgb = srgb;
    r_image.rgba.assign((size_t)w * h * 4, 0.0f);
    const uint32_t blocks_x = (w + 3) / 4;
    const uint32_t blocks_y = (h + 3) / 4;
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = bytes.data() + offset + ((size_t)by * blocks_x + bx) * block_bytes;
            uint8_t px[16 * 4] = {};
            if (format == VK_FORMAT_BC4_UNORM_BLOCK) {
                rgbcx::unpack_bc4(block, px, 4);
                for (uint32_t i = 0; i < 16; i++) {
                    px[i * 4 + 1] = px[i * 4];
                    px[i * 4 + 2] = px[i * 4];
                    px[i * 4 + 3] = 255;
                }
            } else if (format == VK_FORMAT_BC5_UNORM_BLOCK) {
                rgbcx::unpack_bc5(block, px, 0, 1, 4);
                for (uint32_t i = 0; i < 16; i++) px[i * 4 + 3] = 255;
            } else {
                bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(px));
            }

            for (uint32_t ty = 0; ty < 4; ty++) {
                for (uint32_t tx = 0; tx < 4; tx++) {
                    const uint32_t x = bx * 4 + tx;
                    const uint32_t y = by * 4 + ty;
                    if (x >= w || y >= h) continue;
                    const uint8_t* s = px + (ty * 4 + tx) * 4;
                    float* d = r_image.rgba.data() + ((size_t)y * w + x) * 4;
                    const float a = s[3] / 255.0f;
                    for (int c = 0; c < 3; c++) {
                        const float v = s[c] / 255.0f;
                        d[c] = (srgb ? _srgb_to_linear(v) : v) * a;
                    }
                    d[3] = a;
                }
            }
        }
    }
    return true;
}

IconCooker::Image IconCooker::_square(const Image& p_src)
{
    const uint32_t side = std::max(p_src.width, p_src.height);
    if (p_src.width == p_src.height) return p_src;
    Image out;
    out.width = side;
    out.height = side;
    out.srgb = p_src.srgb;
    out.rgba.assign((size_t)side * side * 4, 0.0f);
    const uint32_t ox = (side - p_src.width) / 2;
    const uint32_t oy = (side - p_src.height) / 2;
    for (uint32_t y = 0; y < p_src.height; y++) {
        std::memcpy(out.rgba.data() + (((size_t)(y + oy) * side + ox) * 4), p_src.rgba.data() + (size_t)y * p_src.width * 4, (size_t)p_src.width * 4 * sizeof(float));
    }
    return out;
}

IconCooker::Image IconCooker::_resample(const Image& p_src, uint32_t p_width, uint32_t p_height)
{
    Image out;
    out.width = p_width;
    out.height = p_height;
    out.srgb = p_src.srgb;
    out.rgba.assign((size_t)p_width * p_height * 4, 0.0f);
    const float sx = (float)p_src.width / (float)p_width;
    const float sy = (float)p_src.height / (float)p_height;

    for (uint32_t dy = 0; dy < p_height; dy++) {
        const float y0 = dy * sy;
        const float y1 = y0 + sy;
        for (uint32_t dx = 0; dx < p_width; dx++) {
            const float x0 = dx * sx;
            const float x1 = x0 + sx;
            float acc[4] = {};
            float total = 0.0f;
            for (uint32_t y = (uint32_t)y0; y < std::min((uint32_t)std::ceil(y1), p_src.height); y++) {
                const float wy = std::min(y1, (float)(y + 1)) - std::max(y0, (float)y);
                for (uint32_t x = (uint32_t)x0; x < std::min((uint32_t)std::ceil(x1), p_src.width); x++) {
                    const float wgt = wy * (std::min(x1, (float)(x + 1)) - std::max(x0, (float)x));
                    const float* s = p_src.rgba.data() + ((size_t)y * p_src.width + x) * 4;
                    for (int c = 0; c < 4; c++) acc[c] += s[c] * wgt;
                    total += wgt;
                }
            }
            float* d = out.rgba.data() + ((size_t)dy * p_width + dx) * 4;
            for (int c = 0; c < 4; c++) d[c] = total > 0.0f ? acc[c] / total : 0.0f;
        }
    }
    return out;
}

bool IconCooker::_encode_png(const Image& p_image, std::vector<uint8_t>& r_png)
{
    std::vector<uint8_t> px((size_t)p_image.width * p_image.height * 4);
    for (size_t i = 0; i < (size_t)p_image.width * p_image.height; i++) {
        const float* s = p_image.rgba.data() + i * 4;
        const float a = std::clamp(s[3], 0.0f, 1.0f);
        for (int c = 0; c < 3; c++) {
            const float v = std::clamp(a > 0.0f ? s[c] / a : 0.0f, 0.0f, 1.0f);
            px[i * 4 + c] = (uint8_t)std::lround(std::clamp(p_image.srgb ? _linear_to_srgb(v) : v, 0.0f, 1.0f) * 255.0f);
        }
        px[i * 4 + 3] = (uint8_t)std::lround(a * 255.0f);
    }
    r_png.clear();
    auto write = [](void* p_ctx, void* p_data, int p_size) {
        std::vector<uint8_t>& out = *static_cast<std::vector<uint8_t>*>(p_ctx);
        const uint8_t* d = static_cast<const uint8_t*>(p_data);
        out.insert(out.end(), d, d + p_size);
    };
    return stbi_write_png_to_func(write, &r_png, (int)p_image.width, (int)p_image.height, 4, px.data(), (int)p_image.width * 4) != 0;
}

Error IconCooker::cook(const std::filesystem::path& p_texture_bin, const std::filesystem::path& p_dst)
{
    using enum Error;
    Image source;
    if (!_decode(p_texture_bin, source)) {
        log_write("IconCooker: '%s' isn't a cooked texture the icon cooker can read.", p_texture_bin.string().c_str());
        return FAILED;
    }
    const Image square = _square(source);

    std::vector<std::vector<uint8_t>> pngs;
    for (const uint32_t size : SIZES) {
        std::vector<uint8_t> png;
        if (!_encode_png(_resample(square, size, size), png)) return FAILED;
        pngs.push_back(std::move(png));
    }

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

Error IconCooker::cook_window_icon(const Project& p_project)
{
    using enum Error;
    const std::filesystem::path dst = p_project.window_icon_path();
    std::error_code ec;
    if (p_project.settings.window_icon == Guid{}) {
        std::filesystem::remove(dst, ec);
        return OK;
    }
    return cook(p_project.content_path(p_project.settings.window_icon), dst);
}

}