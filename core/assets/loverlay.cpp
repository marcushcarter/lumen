#include <core/assets/loverlay.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <sstream>

namespace lumen {

const char* overlay_mode_name(OverlayMode p_mode)
{
    switch (p_mode) {
        case OverlayMode::OVERRIDE: return "Override";
        case OverlayMode::ADDITIVE: return "Additive";
    }
    return "Override";
}

OverlayMode overlay_mode_from_name(const std::string& p_name, OverlayMode p_fallback)
{
    if (p_name == "Override") return OverlayMode::OVERRIDE;
    if (p_name == "Additive") return OverlayMode::ADDITIVE;
    return p_fallback;
}

static std::string _guid_text(Guid p_guid)
{
    return !p_guid.valid() ? std::string{} : p_guid.to_string();
}

static void _read_names(const toml::node_view<toml::node>& p_node, std::vector<std::string>& r_names)
{
    const toml::array* list = p_node.as_array();
    if (!list) return;
    for (const toml::node& n : *list) {
        if (const std::optional<std::string> name = n.value<std::string>()) r_names.push_back(*name);
    }
}

static toml::array _names(const std::vector<std::string>& p_names)
{
    toml::array list;
    for (const std::string& n : p_names) list.push_back(n);
    return list;
}

Error LOverlay::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::OVERLAY) return FAILED;

    *this = LOverlay{};
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    auto section = tbl[asset_type_section(AssetType::OVERLAY)];
    source = Guid::from_string(section["source"].value_or(std::string{}));
    reference = Guid::from_string(section["reference"].value_or(std::string{}));
    preview_blend_space = Guid::from_string(section["preview_blend_space"].value_or(std::string{}));
    mode = overlay_mode_from_name(section["mode"].value_or(std::string{}), OverlayMode::OVERRIDE);
    fade = (float)section["fade"].value_or(0.2);
    auto mask_section = section["mask"];
    _read_names(mask_section["branches"], mask.branches);
    _read_names(mask_section["exclude"], mask.exclude);
    mask.depth = (uint32_t)std::max<int64_t>(0, mask_section["depth"].value_or<int64_t>(0));
    return OK;
}

Error LOverlay::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table section{
        { "source", _guid_text(source) },
        { "mode", std::string(overlay_mode_name(mode)) },
        { "fade", (double)fade },
        { "preview_blend_space", _guid_text(preview_blend_space) },
        { "mask", toml::table{
            { "branches", _names(mask.branches) },
            { "exclude", _names(mask.exclude) },
            { "depth", (int64_t)mask.depth } }},
    };
    if (mode == OverlayMode::ADDITIVE) section.insert_or_assign("reference", _guid_text(reference));

    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", _guid_text(guid) },
            { "type", static_cast<int64_t>(AssetType::OVERLAY) } }},
        { std::string(asset_type_section(AssetType::OVERLAY)), std::move(section) },
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
