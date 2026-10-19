#include <editor/assets/import/texture_cooker.h>
#include <core/project/project.h>
#include <core/assets/asset_common.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/write_atomic.h>
#include <editor/editor_context.h>
#include <core/project/project.h>
#include <core/assets/ltexture.h>
#include <core/io/image_io.h>
#include <core/base/tasks.h>
#include <drivers/toml/toml_helpers.h>
#include <vulkan/vulkan.h>
#include <rdo_bc_encoder.h>
#include <utils.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <string>

namespace lumen {

static float srgb_u8_to_linear(uint8_t p_c)
{
    float cs = p_c / 255.0f;
    return (cs <= 0.04045f) ? (cs / 12.92f) : std::pow((cs + 0.055f) / 1.055f, 2.4f);
}

static uint8_t linear_to_srgb_u8(float p_l)
{
    p_l = p_l < 0.0f ? 0.0f : (p_l > 1.0f ? 1.0f : p_l);
    float cs = (p_l <= 0.0031308f) ? (p_l * 12.92f) : (1.055f * std::pow(p_l, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(cs * 255.0f + 0.5f);
}

static std::vector<uint8_t> downsample_rgba8(const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh, bool p_srgb)
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
                float lin = 0.25f * (srgb_u8_to_linear(a[ch]) + srgb_u8_to_linear(b[ch]) + srgb_u8_to_linear(c[ch]) + srgb_u8_to_linear(d[ch]));
                o[ch] = linear_to_srgb_u8(lin);
            }
            else o[ch] = static_cast<uint8_t>((a[ch] + b[ch] + c[ch] + d[ch] + 2) / 4);
        }
        o[3] = static_cast<uint8_t>((a[3] + b[3] + c[3] + d[3] + 2) / 4);
    }
    return dst;
}

static std::vector<uint8_t> downsample_normal_rgba8(const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh)
{
    r_nw = p_w > 1 ? p_w / 2 : 1;
    r_nh = p_h > 1 ? p_h / 2 : 1;
    std::vector<uint8_t> dst(static_cast<size_t>(r_nw) * r_nh * 4);

    auto texel = [&](uint32_t x, uint32_t y) { return &p_src[(static_cast<size_t>(y) * p_w + x) * 4]; };
    auto decode = [](const uint8_t* p, float* r_n) { for (int ch = 0; ch < 3; ++ch) r_n[ch] += p[ch] / 127.5f - 1.0f; };

    for (uint32_t ny = 0; ny < r_nh; ++ny) for (uint32_t nx = 0; nx < r_nw; ++nx) {
        uint32_t x0 = nx * 2, x1 = (x0 + 1 < p_w) ? x0 + 1 : x0;
        uint32_t y0 = ny * 2, y1 = (y0 + 1 < p_h) ? y0 + 1 : y0;
        float n[3] = { 0.0f, 0.0f, 0.0f };
        decode(texel(x0, y0), n); decode(texel(x1, y0), n);
        decode(texel(x0, y1), n); decode(texel(x1, y1), n);
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
        uint8_t* o = &dst[(static_cast<size_t>(ny) * r_nw + nx) * 4];
        for (int ch = 0; ch < 3; ++ch) o[ch] = static_cast<uint8_t>(std::clamp((len > 1e-6f ? n[ch] * inv : (ch == 2 ? 1.0f : 0.0f)) * 127.5f + 127.5f, 0.0f, 255.0f) + 0.5f);
        o[3] = 255;
    }
    return dst;
}

static float alpha_coverage(const std::vector<uint8_t>& p_px, float p_cutoff, float p_scale)
{
    const size_t count = p_px.size() / 4;
    if (count == 0) return 0.0f;
    size_t covered = 0;
    for (size_t i = 0; i < count; ++i) covered += (p_px[i * 4 + 3] / 255.0f) * p_scale > p_cutoff ? 1 : 0;
    return (float)covered / (float)count;
}

static void alpha_coverage_preserve(std::vector<uint8_t>& r_px, float p_cutoff, float p_target)
{
    float lo = 0.0f, hi = 4.0f, best = 1.0f;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        const float c = alpha_coverage(r_px, p_cutoff, mid);
        best = mid;
        if (c < p_target) lo = mid; else hi = mid;
    }
    for (size_t i = 3; i < r_px.size(); i += 4) r_px[i] = static_cast<uint8_t>(std::clamp(r_px[i] * best, 0.0f, 255.0f) + 0.5f);
}

struct FormatMap { DXGI_FORMAT dxgi; uint32_t vk; bool bc45; bool srgb; };

static FormatMap map_format(TextureUsage p_usage, uint8_t p_channels)
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

static void apply_quality(rdo_bc::rdo_bc_params& r_p, TextureQuality p_quality)
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
    std::string stem = p_src.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    auto ends = [&](const char* p_suffix) {
        const size_t n = std::strlen(p_suffix);
        return stem.size() >= n && stem.compare(stem.size() - n, n, p_suffix) == 0;
    };
    if (ends("_n") || ends("_nrm") || ends("_normal") || ends("_ddn") || ends("_nor")) return TextureUsage::NORMAL;
    if (ends("_orm") || ends("_arm") || ends("_rough") || ends("_roughness") || ends("_metal") || ends("_metallic") || ends("_ao") || ends("_mask") || ends("_metallicroughness")) return TextureUsage::MASK;
    return TextureUsage::COLOR;
}

Error TextureCooker::_cook(const Job& p_job)
{
    using enum Error;

    auto report = [&](float v){ if (p_job.progress) p_job.progress->progress.store(v, std::memory_order_relaxed); };
    report(0.0f);

    auto img = ImageIO::load_from_file<uint8_t, 4>(p_job.source.wstring());
    if (!img.valid()) return FAILED;
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
    const FormatMap fmt = map_format(cs.usage, channels);

    if (normal && cs.flip_green) {
        for (size_t i = 1; i < mip0.size(); i += 4) mip0[i] = static_cast<uint8_t>(255 - mip0[i]);
    }

    auto downsample = [&](const std::vector<uint8_t>& p_src, uint32_t p_w, uint32_t p_h, uint32_t& r_nw, uint32_t& r_nh) {
        return normal ? downsample_normal_rgba8(p_src, p_w, p_h, r_nw, r_nh) : downsample_rgba8(p_src, p_w, p_h, r_nw, r_nh, fmt.srgb);
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
    const float target_coverage = keep_coverage ? alpha_coverage(mip0, cs.alpha_cutoff, 1.0f) : 0.0f;

    std::vector<std::vector<uint8_t>> mips{ std::move(mip0) };
    std::vector<std::pair<uint32_t, uint32_t>> dims{ { base_w, base_h } };
    if (cs.generate_mips) {
        uint32_t w = base_w, h = base_h;
        while (w > 1 || h > 1) {
            uint32_t nw, nh;
            mips.push_back(downsample(mips.back(), w, h, nw, nh));
            if (keep_coverage) alpha_coverage_preserve(mips.back(), cs.alpha_cutoff, target_coverage);
            dims.push_back({ nw, nh }); w = nw; h = nh;
        }
    }
    const uint32_t mip_count = static_cast<uint32_t>(mips.size());
    
    report(0.1f);

    std::vector<uint8_t> blocks;
    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t mw = dims[m].first, mh = dims[m].second;
        utils::image_u8 src(mw, mh);
        const std::vector<uint8_t>& buf = mips[m];
        for (uint32_t y = 0; y < mh; ++y) for (uint32_t x = 0; x < mw; ++x) {
            const uint8_t* px = &buf[(static_cast<size_t>(y) * mw + x) * 4];
            src(x, y) = utils::color_quad_u8(px[0], px[1], px[2], px[3]);
        }
        rdo_bc::rdo_bc_params p;
        p.m_dxgi_format = fmt.dxgi;
        apply_quality(p, cs.quality);
        p.m_perceptual = fmt.srgb;
        p.m_rdo_lambda = cs.rdo_lambda;
        p.m_rdo_multithreading = true;
        p.m_status_output = false;
        if (fmt.bc45) { p.m_bc45_channel0 = 0; p.m_bc45_channel1 = 1; }

        rdo_bc::rdo_bc_encoder enc;
        if (!enc.init(src, p)) return FAILED;
        if (!enc.encode()) return FAILED;
        const uint8_t* b = static_cast<const uint8_t*>(enc.get_blocks());
        blocks.insert(blocks.end(), b, b + enc.get_total_blocks_size_in_bytes());

        report(0.1f + 0.8f * (float)(m + 1) / (float)mip_count);
    }

    LTexturePayloadHeader ph{};
    ph.vk_format = fmt.vk;
    ph.width = base_w;
    ph.height = base_h;
    ph.mip_count = mip_count;
    ph.flags = (fmt.srgb ? TEXTURE_FLAG_SRGB : 0u) | (normal ? TEXTURE_FLAG_NORMAL_MAP : 0u);

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

    report(0.95f);

    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_job.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::TEXTURE) } }},
        { std::string(asset_type_section(AssetType::TEXTURE)), toml::table{
            { "path", p_job.source.generic_string() },
            { "width", static_cast<int64_t>(base_w) },
            { "height", static_cast<int64_t>(base_h) },
            { "source_width", static_cast<int64_t>(width) },
            { "source_height", static_cast<int64_t>(height) },
            { "channels", static_cast<int64_t>(channels) },
            { "source_channel", static_cast<int64_t>(cs.source_channel) },
            { "srgb", fmt.srgb },
            { "usage", texture_usage_name(cs.usage) },
            { "quality", texture_quality_name(cs.quality) },
            { "generate_mips", cs.generate_mips },
            { "max_size", static_cast<int64_t>(cs.max_size) },
            { "flip_green", cs.flip_green },
            { "alpha_cutoff", static_cast<double>(cs.alpha_cutoff) },
            { "rdo_lambda", static_cast<double>(cs.rdo_lambda) } }},
    };
    std::string text; { std::ostringstream ss; ss << tbl; text = ss.str(); }
    if (!write_file_atomic(p_job.dst_ltexture, text.data(), text.size())) return FAILED;

    report(1.0f);
    return OK;
}

void TextureCooker::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const TextureCooker::CookSettings& p_settings)
{
    Job job;
    job.source = p_src;
    job.dst_ltexture = p_dst;
    job.settings = p_settings;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::TEXTURE, p_dst, job.guid, job.content_bin) != Error::OK) {
        log_write("Texture import failed: %s", p_src.string().c_str());
        return;
    }
    job.progress = ctx.imports->add(p_dst, job.guid, job.content_bin);
    ctx.tasks->dispatch([job]{
        if (TextureCooker::_cook(job) != Error::OK && job.progress && !job.progress->cancel.load(std::memory_order_relaxed)) job.progress->progress.store(1.0f, std::memory_order_relaxed);
    });
}

}