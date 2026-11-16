#include <core/assets/lterrain.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

Error LTerrain::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::TERRAIN) return FAILED;

    *this = LTerrain{};
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));

    auto import = tbl["import"];
    source = import["source"].value_or(std::string{});
    source_width = (uint32_t)import["source_width"].value_or<int64_t>(0);
    source_height = (uint32_t)import["source_height"].value_or<int64_t>(0);
    source_bits = (uint32_t)import["source_bits"].value_or<int64_t>(0);

    auto terrain = tbl[asset_type_section(AssetType::TERRAIN)];
    samples_x = (uint32_t)terrain["samples_x"].value_or<int64_t>(0);
    samples_y = (uint32_t)terrain["samples_y"].value_or<int64_t>(0);
    tiles_x = (uint32_t)terrain["tiles_x"].value_or<int64_t>(0);
    tiles_y = (uint32_t)terrain["tiles_y"].value_or<int64_t>(0);
    spacing = (float)terrain["spacing"].value_or((double)LTERRAIN_DEFAULT_SPACING);
    height_min = (float)terrain["height_min"].value_or((double)LTERRAIN_DEFAULT_HEIGHT_MIN);
    height_max = (float)terrain["height_max"].value_or((double)LTERRAIN_DEFAULT_HEIGHT_MAX);
    return OK;
}

Error LTerrain::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table tbl{
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", guid.valid() ? guid.to_string() : std::string{} },
            { "type", static_cast<int64_t>(AssetType::TERRAIN) } }},
    };
    if (!source.empty()) {
        tbl.insert("import", toml::table{
            { "source", source },
            { "source_width", static_cast<int64_t>(source_width) },
            { "source_height", static_cast<int64_t>(source_height) },
            { "source_bits", static_cast<int64_t>(source_bits) } });
    }
    tbl.insert(std::string(asset_type_section(AssetType::TERRAIN)), toml::table{
        { "samples_x", static_cast<int64_t>(samples_x) },
        { "samples_y", static_cast<int64_t>(samples_y) },
        { "tiles_x", static_cast<int64_t>(tiles_x) },
        { "tiles_y", static_cast<int64_t>(tiles_y) },
        { "spacing", static_cast<double>(spacing) },
        { "height_min", static_cast<double>(height_min) },
        { "height_max", static_cast<double>(height_max) } });
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

}
