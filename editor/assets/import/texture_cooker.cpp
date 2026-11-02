#include <editor/assets/import/texture_cooker.h>
#include <core/assets/asset_common.h>
#include <core/assets/ltexture.h>
#include <core/base/color.h>
#include <core/base/jobs.h>
#include <core/io/file_io.h>
#include <core/io/image_io.h>
#include <core/project/project.h>
#include <editor/application/editor_context.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <drivers/toml/toml_helpers.h>
#include <vulkan/vulkan.h>
#include <rdo_bc_encoder.h>
#include <utils.h>
#include <stb_image.h>
#include <bc7decomp.h>
#include <rgbcx.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <initializer_list>
#include <iterator>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <string>

namespace lumen {

static float _srgb_u8_to_linear(uint8_t p_c)
{
    return color_srgb_to_linear(p_c / 255.0f);
}

static uint8_t _linear_to_srgb_u8(float p_l)
{
    return static_cast<uint8_t>(color_linear_to_srgb(p_l < 0.0f ? 0.0f : (p_l > 1.0f ? 1.0f : p_l)) * 255.0f + 0.5f);
}

static std::vector<uint8_t> _downsample_rgba8(const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh, bool p_srgb)
{
    r_nw = p_w > 1 ? p_w / 2 : 1;
    r_nh = p_h > 1 ? p_h / 2 : 1;
    std::vector<uint8_t> dst(static_cast<size_t>(r_nw) * r_nh * 4);

    auto texel = [&](uint32_t x, uint32_t y) { return &p_src[(static_cast<size_t>(y) * p_w + x) * 4]; };

    for (uint32_t ny = 0; ny < r_nh; ++ny) for (uint32_t nx = 0; nx < r_nw; ++nx) {
        uint32_t x0 = nx * 2, x1 = (x0 + 1 < p_w) ? x0 + 1 : x0;
        uint32_t y0 = ny * 2, y1 = (y0 + 1 < p_h) ? y0 + 1 : y0;
        const uint8_t* a = texel(x0, y0); const uint8_t* b = texel(x1, y0);
        const uint8_t* c = texel(x0, y1); const uint8_t* d = texel(x1, y1);
        uint8_t* o = &dst[(static_cast<size_t>(ny) * r_nw + nx) * 4];
        for (int ch = 0; ch < 3; ++ch) {
            if (p_srgb) {
                float lin = 0.25f * (_srgb_u8_to_linear(a[ch]) + _srgb_u8_to_linear(b[ch]) + _srgb_u8_to_linear(c[ch]) + _srgb_u8_to_linear(d[ch]));
                o[ch] = _linear_to_srgb_u8(lin);
            }
            else o[ch] = static_cast<uint8_t>((a[ch] + b[ch] + c[ch] + d[ch] + 2) / 4);
        }
        o[3] = static_cast<uint8_t>((a[3] + b[3] + c[3] + d[3] + 2) / 4);
    }
    return dst;
}

static std::vector<uint8_t> _downsample_normal_rgba8(const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh)
{
    r_nw = p_w > 1 ? p_w / 2 : 1;
    r_nh = p_h > 1 ? p_h / 2 : 1;
    std::vector<uint8_t> dst(static_cast<size_t>(r_nw) * r_nh * 4);

    auto texel = [&](uint32_t x, uint32_t y) { return &p_src[(static_cast<size_t>(y) * p_w + x) * 4]; };
    auto decode = [](const uint8_t* p, float* r_n) {
        const float x = p[0] / 127.5f - 1.0f;
        const float y = p[1] / 127.5f - 1.0f;
        r_n[0] += x;
        r_n[1] += y;
        r_n[2] += std::sqrt(std::max(1.0f - x * x - y * y, 0.0f));
    };

    for (uint32_t ny = 0; ny < r_nh; ++ny) for (uint32_t nx = 0; nx < r_nw; ++nx) {
        uint32_t x0 = nx * 2, x1 = (x0 + 1 < p_w) ? x0 + 1 : x0;
        uint32_t y0 = ny * 2, y1 = (y0 + 1 < p_h) ? y0 + 1 : y0;
        float n[3] = { 0.0f, 0.0f, 0.0f };
        decode(texel(x0, y0), n); decode(texel(x1, y0), n);
        decode(texel(x0, y1), n); decode(texel(x1, y1), n);
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
        uint8_t* o = &dst[(static_cast<size_t>(ny) * r_nw + nx) * 4];
        for (int ch = 0; ch < 2; ++ch) o[ch] = static_cast<uint8_t>(std::clamp((len > 1e-6f ? n[ch] * inv : 0.0f) * 127.5f + 127.5f, 0.0f, 255.0f) + 0.5f);
        o[2] = 0;
        o[3] = 255;
    }
    return dst;
}

static float _alpha_coverage(const std::vector<uint8_t>& p_px, float p_cutoff, float p_scale)
{
    const size_t count = p_px.size() / 4;
    if (count == 0) return 0.0f;
    size_t covered = 0;
    for (size_t i = 0; i < count; ++i) covered += (p_px[i * 4 + 3] / 255.0f) * p_scale > p_cutoff ? 1 : 0;
    return (float)covered / (float)count;
}

static void _alpha_coverage_preserve(std::vector<uint8_t>& r_px, float p_cutoff, float p_target)
{
    float lo = 0.0f, hi = 4.0f, best = 1.0f;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        const float c = _alpha_coverage(r_px, p_cutoff, mid);
        best = mid;
        if (c < p_target) lo = mid; else hi = mid;
    }
    for (size_t i = 3; i < r_px.size(); i += 4) r_px[i] = static_cast<uint8_t>(std::clamp(r_px[i] * best, 0.0f, 255.0f) + 0.5f);
}

struct FormatMap { DXGI_FORMAT dxgi; uint32_t vk; bool bc45; bool srgb; };

static FormatMap _map_format(TextureUsage p_usage, uint8_t p_channels)
{
    switch (p_usage) {
        case TextureUsage::COLOR: return { DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_SRGB_BLOCK, false, true };
        case TextureUsage::NORMAL: return { DXGI_FORMAT_BC5_UNORM, VK_FORMAT_BC5_UNORM_BLOCK, true, false };
        case TextureUsage::MASK:
        case TextureUsage::LINEAR:
            if (p_channels == 1) return { DXGI_FORMAT_BC4_UNORM, VK_FORMAT_BC4_UNORM_BLOCK, true, false };
            if (p_channels == 2 && p_usage == TextureUsage::LINEAR) return { DXGI_FORMAT_BC5_UNORM, VK_FORMAT_BC5_UNORM_BLOCK, true, false };
            return { DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_UNORM_BLOCK, false, false };
    }
    return { DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_SRGB_BLOCK, false, true };
}

static void _apply_quality(rdo_bc::rdo_bc_params& r_p, TextureQuality p_quality)
{
    switch (p_quality) {
        case TextureQuality::FAST:
            r_p.m_bc7_uber_level = 0;
            r_p.m_bc7enc_max_partitions_to_scan = 16;
            r_p.m_use_hq_bc345 = false;
            r_p.m_bc345_search_rad = 1;
            break;
        case TextureQuality::NORMAL:
            r_p.m_bc7_uber_level = 2;
            r_p.m_bc7enc_max_partitions_to_scan = BC7ENC_MAX_PARTITIONS;
            r_p.m_use_hq_bc345 = true;
            r_p.m_bc345_search_rad = 3;
            break;
        case TextureQuality::BEST:
            r_p.m_bc7_uber_level = BC7ENC_MAX_UBER_LEVEL;
            r_p.m_bc7enc_max_partitions_to_scan = BC7ENC_MAX_PARTITIONS;
            r_p.m_use_hq_bc345 = true;
            r_p.m_bc345_search_rad = 5;
            break;
    }
}

TextureUsage texture_usage_guess(const std::filesystem::path& p_src)
{
    const std::string stem = p_src.stem().string();
    std::vector<std::string> tokens(1);
    char prev = 0;
    for (char c : stem) {
        if (c == '_' || c == '-' || c == ' ' || c == '.') {
            if (!tokens.back().empty()) tokens.emplace_back();
        } else {
            if (std::isupper((unsigned char)c) && std::islower((unsigned char)prev) && !tokens.back().empty()) tokens.emplace_back();
            tokens.back() += (char)std::tolower((unsigned char)c);
        }
        prev = c;
    }
    auto any = [&](std::initializer_list<const char*> p_words) {
        for (const std::string& t : tokens) for (const char* w : p_words) if (t == w) return true;
        return false;
    };
    if (any({ "n", "nrm", "nor", "norm", "normal", "normals", "normalmap", "normalgl", "normaldx", "nrmgl", "nrmdx", "ddn" })) return TextureUsage::NORMAL;
    if (any({ "orm", "arm", "rough", "roughness", "metal", "metallic", "metalness", "ao", "occlusion", "ambientocclusion", "mask", "metallicroughness", "specular", "gloss", "glossiness" })) return TextureUsage::MASK;
    if (any({ "height", "heightmap", "displacement", "disp", "bump", "depth" })) return TextureUsage::LINEAR;
    return TextureUsage::COLOR;
}

Error TextureCooker::_cook(const Job& p_job)
{
    using enum Error;

    const ImportStep& step = p_job.step;

    auto img = ImageIO::load_from_file<uint8_t, 4>(p_job.source.wstring());
    if (!img.valid()) return FAILED;
    step.report(LOAD_SHARE);
    if (step.cancelled()) return CANCELED;
    const uint32_t width = static_cast<uint32_t>(img.width);
    const uint32_t height = static_cast<uint32_t>(img.height);
    uint8_t channels = static_cast<uint8_t>(img.source_channels);
    std::vector<uint8_t> mip0(reinterpret_cast<uint8_t*>(img.pixels), reinterpret_cast<uint8_t*>(img.pixels) + static_cast<size_t>(width) * height * 4);
    ImageIO::free_image(img);

    const CookSettings& cs = p_job.settings;
    if (cs.source_channel >= 0 && cs.source_channel < 4) {
        for (size_t i = 0; i < mip0.size(); i += 4) {
            const uint8_t v = mip0[i + (size_t)cs.source_channel];
            mip0[i + 0] = v;
            mip0[i + 1] = v;
            mip0[i + 2] = v;
            mip0[i + 3] = 255;
        }
        channels = 1;
    }
    const bool normal = cs.usage == TextureUsage::NORMAL;
    if (normal) {
        for (size_t i = 0; i < mip0.size(); i += 4) {
            if (cs.flip_green) mip0[i + 1] = static_cast<uint8_t>(255 - mip0[i + 1]);
            mip0[i + 2] = 0;
            mip0[i + 3] = 255;
        }
        channels = 2;
    }
    const FormatMap fmt = _map_format(cs.usage, channels);

    auto downsample = [&](const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh) {
        return normal ? _downsample_normal_rgba8(p_src, p_w, p_h, r_nw, r_nh) : _downsample_rgba8(p_src, p_w, p_h, r_nw, r_nh, fmt.srgb);
    };

    uint32_t base_w = width, base_h = height;
    if (cs.max_size > 0) {
        while (base_w > cs.max_size || base_h > cs.max_size) {
            uint32_t nw, nh;
            mip0 = downsample(mip0, base_w, base_h, nw, nh);
            base_w = nw; base_h = nh;
        }
    }

    const bool keep_coverage = cs.usage == TextureUsage::COLOR && cs.alpha_cutoff > 0.0f;
    const float target_coverage = keep_coverage ? _alpha_coverage(mip0, cs.alpha_cutoff, 1.0f) : 0.0f;

    std::vector<std::vector<uint8_t>> mips{ std::move(mip0) };
    std::vector<std::pair<uint32_t, uint32_t>> dims{ { base_w, base_h } };
    if (cs.generate_mips) {
        uint32_t w = base_w, h = base_h;
        while (w > 1 || h > 1) {
            uint32_t nw, nh;
            mips.push_back(downsample(mips.back(), w, h, nw, nh));
            if (keep_coverage) _alpha_coverage_preserve(mips.back(), cs.alpha_cutoff, target_coverage);
            dims.push_back({ nw, nh }); w = nw; h = nh;
        }
    }
    const uint32_t mip_count = static_cast<uint32_t>(mips.size());

    step.report(MIPS_SHARE);
    if (step.cancelled()) return CANCELED;

    uint64_t total_px = 0;
    for (const auto& d : dims) total_px += (uint64_t)d.first * d.second;
    uint64_t done_px = 0;
    const ImportStep encode_step = step.sub(MIPS_SHARE, ENCODE_END);

    rdo_bc::rdo_bc_params p;
    p.m_dxgi_format = fmt.dxgi;
    _apply_quality(p, cs.quality);
    p.m_perceptual = fmt.srgb;
    p.m_rdo_lambda = cs.rdo_lambda;
    p.m_rdo_multithreading = true;
    p.m_status_output = false;
    if (fmt.bc45) { p.m_bc45_channel0 = 0; p.m_bc45_channel1 = 1; }

    std::vector<uint8_t> blocks;
    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t mw = dims[m].first, mh = dims[m].second;
        const std::vector<uint8_t>& buf = mips[m];
        for (uint32_t y0 = 0; y0 < mh; y0 += ENCODE_STRIP_ROWS) {
            const uint32_t sh = std::min(ENCODE_STRIP_ROWS, mh - y0);
            utils::image_u8 src(mw, sh);
            for (uint32_t y = 0; y < sh; ++y) for (uint32_t x = 0; x < mw; ++x) {
                const uint8_t* px = &buf[(static_cast<size_t>(y0 + y) * mw + x) * 4];
                src(x, y) = utils::color_quad_u8(px[0], px[1], px[2], px[3]);
            }
            rdo_bc::rdo_bc_encoder enc;
            if (!enc.init(src, p)) return FAILED;
            if (!enc.encode()) return FAILED;
            const uint8_t* b = static_cast<const uint8_t*>(enc.get_blocks());
            blocks.insert(blocks.end(), b, b + enc.get_total_blocks_size_in_bytes());

            done_px += (uint64_t)mw * sh;
            encode_step.report((float)((double)done_px / (double)total_px));
            if (step.cancelled()) return CANCELED;
        }
    }

    LTexturePayloadHeader ph{};
    ph.vk_format = fmt.vk;
    ph.width = base_w;
    ph.height = base_h;
    ph.mip_count = mip_count;
    ph.flags = (uint32_t)((fmt.srgb ? TextureFlags::SRGB : TextureFlags::NONE) | (normal ? TextureFlags::NORMAL_MAP : TextureFlags::NONE));

    LAssetHeader ah{};
    ah.magic = BCON_MAGIC;
    ah.version = LASSET_VERSION;
    ah.guid = p_job.guid;
    ah.type = AssetType::TEXTURE;
    ah.payload_size = static_cast<uint32_t>(sizeof(ph) + blocks.size());

    std::vector<uint8_t> bin;
    bin.reserve(sizeof(ah) + sizeof(ph) + blocks.size());
    auto append = [&](const void* d, size_t n){ const uint8_t* p = static_cast<const uint8_t*>(d); bin.insert(bin.end(), p, p + n); };
    append(&ah, sizeof(ah));
    append(&ph, sizeof(ph));
    append(blocks.data(), blocks.size());

    std::error_code ec;
    std::filesystem::create_directories(p_job.content_bin.parent_path(), ec);
    if (!write_file_atomic(p_job.content_bin, bin.data(), bin.size())) return FAILED;

    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_job.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::TEXTURE) } }},
        { "import", toml::table{
            { "source", p_job.source.generic_string() },
            { "source_width", static_cast<int64_t>(width) },
            { "source_height", static_cast<int64_t>(height) },
            { "source_channel", static_cast<int64_t>(cs.source_channel) },
            { "max_size", static_cast<int64_t>(cs.max_size) },
            { "generate_mips", cs.generate_mips },
            { "quality", texture_quality_name(cs.quality) },
            { "rdo_lambda", static_cast<double>(cs.rdo_lambda) },
            { "flip_green", cs.flip_green },
            { "alpha_cutoff", static_cast<double>(cs.alpha_cutoff) } }},
        { std::string(asset_type_section(AssetType::TEXTURE)), toml::table{
            { "usage", texture_usage_name(cs.usage) },
            { "width", static_cast<int64_t>(base_w) },
            { "height", static_cast<int64_t>(base_h) },
            { "channels", static_cast<int64_t>(channels) },
            { "srgb", fmt.srgb } }},
    };
    std::string text; { std::ostringstream ss; ss << tbl; text = ss.str(); }
    if (!write_file_atomic(p_job.dst_ltexture, text.data(), text.size())) return FAILED;

    step.report(1.0f);
    return OK;
}

uint64_t TextureCooker::cost(const std::filesystem::path& p_src, const CookSettings& p_settings)
{
    int w = 0, h = 0, c = 0;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p_src.wstring().c_str(), L"rb") != 0 || !f) return 1;
    const bool ok = stbi_info_from_file(f, &w, &h, &c) != 0;
    std::fclose(f);
    if (!ok || w <= 0 || h <= 0) return 1;
    uint64_t pw = (uint64_t)w, ph = (uint64_t)h;
    while (p_settings.max_size > 0 && (pw > p_settings.max_size || ph > p_settings.max_size)) { pw = std::max<uint64_t>(pw / 2, 1); ph = std::max<uint64_t>(ph / 2, 1); }
    return pw * ph * 4 / 3;
}

void TextureCooker::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const TextureCooker::CookSettings& p_settings)
{
    Job job;
    job.source = p_src;
    job.settings = p_settings;
    std::filesystem::path content_bin;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::TEXTURE, p_dst, job.guid, content_bin) != Error::OK) {
        log_write("Texture import failed: %s", p_src.string().c_str());
        return;
    }
    std::shared_ptr<ImportSession> session = ctx.imports->begin(*ctx.project, p_dst);
    if (!session) return;
    const ImportSession::Staged staged = session->stage(*ctx.project, p_dst, job.guid, true, true);
    job.dst_ltexture = staged.asset;
    job.content_bin = staged.content_bin;
    job.step = session->step();
    job_system().run_background_owned([job, session]{
        session->finish(TextureCooker::_cook(job));
    }, nullptr, "Texture Import");
}


static size_t _mip_bytes(uint32_t p_w, uint32_t p_h, uint32_t p_block_bytes)
{
    return (size_t)((p_w + 3) / 4) * ((p_h + 3) / 4) * p_block_bytes;
}

bool TextureCooker::decode(const std::filesystem::path& p_content_bin, ImageData<uint8_t, 4>& r_image, bool& r_srgb, uint32_t p_min_side)
{
    std::ifstream in(p_content_bin, std::ios::binary);
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

    size_t offset = sizeof(ah) + sizeof(ph);
    uint32_t w = ph.width;
    uint32_t h = ph.height;
    for (uint32_t mip = 0; mip + 1 < ph.mip_count; mip++) {
        const uint32_t nw = std::max(w / 2, 1u);
        const uint32_t nh = std::max(h / 2, 1u);
        if (std::max(nw, nh) < p_min_side) break;
        offset += _mip_bytes(w, h, block_bytes);
        w = nw;
        h = nh;
    }
    if (offset + _mip_bytes(w, h, block_bytes) > bytes.size()) return false;

    r_image = ImageIO::create<uint8_t, 4>((int)w, (int)h);
    if (!r_image.valid()) return false;
    r_srgb = format == VK_FORMAT_BC7_SRGB_BLOCK;
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
                    std::memcpy(r_image.pixels + ((size_t)y * w + x) * 4, px + (ty * 4 + tx) * 4, 4);
                }
            }
        }
    }
    return true;
}

}
