#include <core/assets/lblendspace.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

static std::string _guid_text(Guid p_guid)
{
    return !p_guid.valid() ? std::string{} : p_guid.to_string();
}

Error LBlendSpace::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::BLEND_SPACE) return FAILED;

    *this = LBlendSpace{};
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    auto section = tbl[asset_type_section(AssetType::BLEND_SPACE)];
    preview_mesh = Guid::from_string(section["preview_mesh"].value_or(std::string{}));
    axis = section["axis"].value_or(std::string("speed"));
    if (const toml::array* list = section["samples"].as_array()) {
        for (const toml::node& n : *list) {
            const toml::table* st = n.as_table();
            if (!st) continue;
            samples.push_back({ Guid::from_string((*st)["clip"].value_or(std::string{})), (float)(*st)["position"].value_or(0.0), (float)(*st)["direction"].value_or(0.0) });
        }
    }
    return OK;
}

Error LBlendSpace::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::array list;
    for (const BlendSample& b : samples) {
        list.push_back(toml::table{ { "clip", _guid_text(b.clip) }, { "position", (double)b.position }, { "direction", (double)b.direction } });
    }

    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", _guid_text(guid) },
            { "type", static_cast<int64_t>(AssetType::BLEND_SPACE) } }},
        { std::string(asset_type_section(AssetType::BLEND_SPACE)), toml::table{
            { "preview_mesh", _guid_text(preview_mesh) },
            { "axis", axis },
            { "samples", std::move(list) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
