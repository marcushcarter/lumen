#include <core/assets/lworld.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

Error LWorld::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::WORLD) return FAILED;

    *this = LWorld{};
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    return OK;
}

Error LWorld::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", guid.valid() ? guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(AssetType::WORLD) } }},
        { std::string(asset_type_section(AssetType::WORLD)), toml::table{} },
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
