#include <core/assets/lmaterial.h>
#include <core/assets/asset_common.h>
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <string>

namespace lumen {

const char* shading_model_name(ShadingModel p_model)
{
    switch (p_model) {
        case ShadingModel::SKY: return "Sky";
        case ShadingModel::DEFAULT_LIT: return "Default Lit";
        case ShadingModel::UNLIT: return "Unlit";
        case ShadingModel::FOLIAGE: return "Foliage";
        case ShadingModel::SKIN: return "Skin";
        case ShadingModel::CLOTH: return "Cloth";
        case ShadingModel::HAIR: return "Hair";
        case ShadingModel::EYE: return "Eye";
    }
    return "default_lit";
}

ShadingModel shading_model_from_name(const std::string& p_name, ShadingModel p_fallback)
{
    if (p_name == "Sky") return ShadingModel::SKY;
    if (p_name == "Default Lit") return ShadingModel::DEFAULT_LIT;
    if (p_name == "Unlit") return ShadingModel::UNLIT;
    if (p_name == "Foliage") return ShadingModel::FOLIAGE;
    if (p_name == "Skin") return ShadingModel::SKIN;
    if (p_name == "Cloth") return ShadingModel::CLOTH;
    if (p_name == "Hair") return ShadingModel::HAIR;
    if (p_name == "Eye") return ShadingModel::EYE;
    return p_fallback;
}

const char* alpha_mode_name(AlphaMode p_mode)
{
    switch (p_mode) {
        case AlphaMode::NONE: return "Opaque";
        case AlphaMode::MASKED: return "Masked";
    }
    return "Opaque";
}

AlphaMode alpha_mode_from_name(const std::string& p_name, AlphaMode p_fallback)
{
    if (p_name == "Opaque") return AlphaMode::NONE;
    if (p_name == "Masked") return AlphaMode::MASKED;
    return p_fallback;
}

static std::string _guid_text(Guid p_guid)
{
    return p_guid == Guid{} ? std::string{} : p_guid.to_string();
}

static Guid _read_guid(toml::node_view<const toml::node> p_node)
{
    return Guid::from_string(p_node.value_or(std::string{}));
}

static Guid _read_texture(toml::node_view<const toml::node> p_node)
{
    if (p_node.as_table()) return _read_guid(p_node["texture"]);
    return _read_guid(p_node);
}

static float _read_float(toml::node_view<const toml::node> p_node, float p_fallback)
{
    return (float)p_node.value_or((double)p_fallback);
}

template <int N>
static vec<N, float> _read_vec(toml::node_view<const toml::node> p_node, vec<N, float> p_fallback)
{
    const toml::array* arr = p_node.as_array();
    if (!arr || arr->size() != (size_t)N) return p_fallback;
    vec<N, float> v = p_fallback;
    for (int i = 0; i < N; ++i) v[i] = (float)(*arr)[(size_t)i].value_or((double)p_fallback[i]);
    return v;
}

template <int N>
static toml::array _write_vec(vec<N, float> p_v)
{
    toml::array arr;
    for (int i = 0; i < N; ++i) arr.push_back((double)p_v[i]);
    return arr;
}

Error LMaterial::load(const std::filesystem::path& p_path)
{
    using enum Error;
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return FAILED; }

    const toml::table& t = tbl;
    auto asset = t["asset"];
    if (asset_type_from_u32((uint32_t)asset["type"].value_or<int64_t>(0)) != AssetType::MATERIAL) return FAILED;

    const LMaterial def{};
    *this = def;
    guid = _read_guid(asset["guid"]);

    auto m = t[asset_type_section(AssetType::MATERIAL)];
    shading_model = shading_model_from_name(m["shading_model"].value_or(std::string{}), def.shading_model);
    alpha_mode = alpha_mode_from_name(m["alpha_mode"].value_or(std::string{}), def.alpha_mode);
    two_sided = m["two_sided"].value_or(def.two_sided);
    alpha_cutoff = _read_float(m["alpha_cutoff"], def.alpha_cutoff);

    base_color = _read_vec<4>(m["base_color"], def.base_color);
    base_color_texture = _read_texture(m["base_color_texture"]);

    normal_texture = _read_texture(m["normal_texture"]);
    normal_scale = _read_float(m["normal_scale"], def.normal_scale);

    roughness = _read_float(m["roughness"], def.roughness);
    roughness_texture = _read_texture(m["roughness_texture"]);
    metallic = _read_float(m["metallic"], def.metallic);
    metallic_texture = _read_texture(m["metallic_texture"]);
    ao_strength = _read_float(m["ao_strength"], def.ao_strength);
    ao_texture = _read_texture(m["ao_texture"]);

    emissive_color = _read_vec<3>(m["emissive_color"], def.emissive_color);
    emissive_nits = _read_float(m["emissive_nits"], def.emissive_nits);
    emissive_texture = _read_texture(m["emissive_texture"]);

    custom = _read_vec<4>(m["custom"], def.custom);
    custom_texture = _read_texture(m["custom_texture"]);

    uv_scale = _read_vec<2>(m["uv_scale"], def.uv_scale);
    uv_offset = _read_vec<2>(m["uv_offset"], def.uv_offset);

    if (const toml::table* rec = t["import"].as_table()) {
        import.source = (*rec)["source"].value_or(std::string{});
        import.name = (*rec)["name"].value_or(std::string{});
    }
    return OK;
}

Error LMaterial::save(const std::filesystem::path& p_path) const
{
    using enum Error;
    toml::table m {
        { "shading_model", shading_model_name(shading_model) },
        { "alpha_mode", alpha_mode_name(alpha_mode) },
        { "alpha_cutoff", (double)alpha_cutoff },
        { "two_sided", two_sided },
        { "base_color", _write_vec<4>(base_color) },
        { "base_color_texture", _guid_text(base_color_texture) },
        { "normal_texture", _guid_text(normal_texture) },
        { "normal_scale", (double)normal_scale },
        { "roughness", (double)roughness },
        { "roughness_texture", _guid_text(roughness_texture) },
        { "metallic", (double)metallic },
        { "metallic_texture", _guid_text(metallic_texture) },
        { "ao_strength", (double)ao_strength },
        { "ao_texture", _guid_text(ao_texture) },
        { "emissive_color", _write_vec<3>(emissive_color) },
        { "emissive_nits", (double)emissive_nits },
        { "emissive_texture", _guid_text(emissive_texture) },
        { "custom", _write_vec<4>(custom) },
        { "custom_texture", _guid_text(custom_texture) },
        { "uv_scale", _write_vec<2>(uv_scale) },
        { "uv_offset", _write_vec<2>(uv_offset) },
    };
    toml::table tbl {
        { "asset", toml::table{
            { "version", (int64_t)LASSET_VERSION },
            { "guid", guid.to_string() },
            { "type", (int64_t)AssetType::MATERIAL } }},
        { std::string(asset_type_section(AssetType::MATERIAL)), std::move(m) },
    };
    if (!import.source.empty()) {
        tbl.insert_or_assign("import", toml::table{
            { "source", import.source },
            { "name", import.name } });
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
