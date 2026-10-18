#include <core/assets/lmodel.h>
#include <core/assets/asset_common.h>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <string>

namespace lumen {

Error LModel::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    auto asset = tbl["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::MODEL) return FAILED;
    guid = Guid::from_string(asset["guid"].value_or(std::string{}));
    mesh = Guid::from_string(tbl["model"]["mesh"].value_or(std::string{}));
    return OK;
}

Error LModel::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table tbl {
        { "asset", toml::table{
            { "version", (int64_t)LASSET_VERSION },
            { "guid", guid.to_string() },
            { "type", (int64_t)AssetType::MODEL } }},
        { std::string(asset_type_section(AssetType::MODEL)), toml::table{
            { "mesh", mesh.to_string() } }},
    };
    std::ostringstream ss;
    ss << tbl;
    const std::string text = ss.str();

    std::filesystem::path tmp = p_path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return FAILED;
        out.write(text.data(), (std::streamsize)text.size());
        if (!out) return FAILED;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p_path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return FAILED;
    }
    return OK;
}

}