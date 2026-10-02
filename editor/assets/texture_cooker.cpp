#include <editor/assets/texture_cooker.h>
#include <core/project/project.h>
#include <core/assets/asset_common.h>
#include <editor/assets/asset_import_tracker.h>
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

struct FormatMap { DXGI_FORMAT dxgi; uint32_t vk; bool bc45; bool srgb; };

static FormatMap map_format(uint8_t p_channels, bool p_srgb)
{
    switch (p_channels) {
        case 1: return { DXGI_FORMAT_BC4_UNORM, VK_FORMAT_BC4_UNORM_BLOCK, true, false };
        case 2: return { DXGI_FORMAT_BC5_UNORM, VK_FORMAT_BC5_UNORM_BLOCK, true, false };
        default: return p_srgb ? FormatMap{ DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_SRGB_BLOCK, false, true } : FormatMap{ DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_UNORM_BLOCK, false, false };
    }
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
    const uint8_t channels = static_cast<uint8_t>(img.source_channels);
    std::vector<uint8_t> mip0(reinterpret_cast<uint8_t*>(img.pixels), reinterpret_cast<uint8_t*>(img.pixels) + static_cast<size_t>(width) * height * 4);
    ImageIO::free_image(img);

    const FormatMap fmt = map_format(channels, p_job.settings.srgb);

    std::vector<std::vector<uint8_t>> mips{ std::move(mip0) };
    std::vector<std::pair<uint32_t, uint32_t>> dims{ { width, height } };
    if (p_job.settings.generate_mips) {
        uint32_t w = width, h = height;
        while (w > 1 || h > 1) {
            uint32_t nw, nh;
            mips.push_back(downsample_rgba8(mips.back(), w, h, nw, nh, fmt.srgb));
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
        p.m_bc7_uber_level = 4; // move to 6 to have better compression but takes longer (around 4x)
        p.m_perceptual = fmt.srgb;
        p.m_rdo_lambda = 0.3f;
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
    ph.width = width;
    ph.height = height;
    ph.mip_count = mip_count;
    ph.flags = fmt.srgb ? TEXTURE_FLAG_SRGB : 0u;

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
            { "width", static_cast<int64_t>(width) },
            { "height", static_cast<int64_t>(width) },
            { "channels", static_cast<int64_t>(channels) },
            { "srgb", p_job.settings.srgb },
            { "generate_mips", p_job.settings.generate_mips } }},
    };
    std::string text; { std::ostringstream ss; ss << tbl; text = ss.str(); }
    if (!write_file_atomic(p_job.dst_ltexture, text.data(), text.size())) return FAILED;

    report(1.0f);
    return OK;
}

Error TextureCooker::import(const Project& p_project, const std::filesystem::path& p_src, const std::filesystem::path& p_dst, Guid& r_guid, const CookSettings& p_settings)
{
    using enum Error;
    Job job;
    job.source = p_src;
    job.dst_ltexture = p_dst;
    job.settings = p_settings;
    if (AssetImportTracker::resolve_import(p_project, AssetType::TEXTURE, p_dst, job.guid, job.content_bin) != OK) return FAILED;
    if (_cook(job) != OK) return FAILED;
    r_guid = job.guid;
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