#include <core/assets/lmodel.h>
#include <core/assets/asset_common.h>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <string>

namespace lumen {

const char* model_import_mode_name(ModelImportMode p_mode)
{
    switch (p_mode) {
        case ModelImportMode::FULL: return "full";
        case ModelImportMode::MESH_ONLY: return "mesh_only";
    }
    return "full";
}

ModelImportMode model_import_mode_from_name(const std::string& p_name, ModelImportMode p_fallback)
{
    if (p_name == "full") return ModelImportMode::FULL;
    if (p_name == "mesh_only") return ModelImportMode::MESH_ONLY;
    return p_fallback;
}

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
    materials.clear();
    if (const toml::array* arr = tbl["model"]["materials"].as_array()) {
        for (const toml::node& n : *arr) materials.push_back(Guid::from_string(n.value_or(std::string{})));
    }

    import = ImportRecord{};
    if (const toml::table* rec = tbl["import"].as_table()) {
        import.source = (*rec)["source"].value_or(std::string{});
        const ModelImportMode legacy = (*rec)["textures"].value_or(true) ? ModelImportMode::FULL : ModelImportMode::MESH_ONLY;
        import.mode = model_import_mode_from_name((*rec)["mode"].value_or(std::string{}), legacy);
    }
    return OK;
}

Error LModel::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::array slots;
    for (Guid m : materials) slots.push_back(m == Guid{} ? std::string{} : m.to_string());
    toml::table tbl {
        { "asset", toml::table{
            { "version", (int64_t)LASSET_VERSION },
            { "guid", guid.to_string() },
            { "type", (int64_t)AssetType::MODEL } }},
        { std::string(asset_type_section(AssetType::MODEL)), toml::table{
            { "mesh", mesh.to_string() },
            { "materials", std::move(slots) } }},
    };
    if (!import.source.empty()) {
        tbl.insert_or_assign("import", toml::table{
            { "source", import.source },
            { "mode", model_import_mode_name(import.mode) } });
    }
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