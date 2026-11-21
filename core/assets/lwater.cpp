#include <core/assets/lwater.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

Error LWater::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::WATER) return FAILED;

    *this = LWater{};
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));

    auto water = tbl[asset_type_section(AssetType::WATER)];
    terrain = Guid::from_string(water["terrain"].value_or(std::string{}));
    cell_size = (float)water["cell_size"].value_or(0.0);
    tiles = (uint32_t)water["tiles"].value_or<int64_t>(0);
    wet_area = (float)water["wet_area"].value_or(0.0);
    bake_seconds = (float)water["bake_seconds"].value_or(0.0);
    file_bytes = (uint64_t)water["file_bytes"].value_or<int64_t>(0);
    baked_at = water["baked_at"].value_or(std::string{});
    return OK;
}

Error LWater::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", guid.valid() ? guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(AssetType::WATER) } }},
    };
    tbl.insert(std::string(asset_type_section(AssetType::WATER)), toml::table{
        { "terrain", terrain.valid() ? terrain.to_string() : std::string{} },
        { "cell_size", static_cast<double>(cell_size) },
        { "tiles", static_cast<int64_t>(tiles) },
        { "wet_area", static_cast<double>(wet_area) },
        { "bake_seconds", static_cast<double>(bake_seconds) },
        { "file_bytes", static_cast<int64_t>(file_bytes) },
        { "baked_at", baked_at } });
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
