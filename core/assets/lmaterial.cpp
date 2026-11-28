#include <core/assets/lmaterial.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <cstring>
#include <sstream>
#include <string>

namespace lumen {

const char* alpha_mode_name(AlphaMode p_mode)
{
    switch (p_mode) {
        case AlphaMode::NONE: return "Opaque";
        case AlphaMode::MASKED: return "Masked";
        case AlphaMode::BLEND: return "Transparent";
    }
    return "Opaque";
}

AlphaMode alpha_mode_from_name(const std::string& p_name, AlphaMode p_fallback)
{
    if (p_name == "Opaque") return AlphaMode::NONE;
    if (p_name == "Masked") return AlphaMode::MASKED;
    if (p_name == "Transparent") return AlphaMode::BLEND;
    return p_fallback;
}

static std::string _guid_text(Guid p_guid)
{
    return !p_guid.valid() ? std::string{} : p_guid.to_string();
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
    if (const toml::array* c = m["color"].as_array(); c && c->size() == 3) {
        for (size_t i = 0; i < 3; ++i) color[(int)i] = (float)(*c)[i].value_or((double)def.color[(int)i]);
    }
    base_color_texture = _read_texture(m["base_color_texture"]);
    normal_texture = _read_texture(m["normal_texture"]);
    alpha_mode = alpha_mode_from_name(m["alpha_mode"].value_or(std::string{}), def.alpha_mode);
    two_sided = m["two_sided"].value_or(def.two_sided);
    alpha_cutoff = _read_float(m["alpha_cutoff"], def.alpha_cutoff);
    mask_texture = _read_texture(m["mask_texture"]);
    mask_channel = texture_channel_from_name(m["mask_channel"].value_or(std::string{}), def.mask_channel);

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
        { "color", toml::array{ (double)color.x, (double)color.y, (double)color.z } },
        { "base_color_texture", _guid_text(base_color_texture) },
        { "normal_texture", _guid_text(normal_texture) },
        { "alpha_mode", alpha_mode_name(alpha_mode) },
        { "alpha_cutoff", (double)alpha_cutoff },
        { "two_sided", two_sided },
        { "mask_texture", _guid_text(mask_texture) },
        { "mask_channel", texture_channel_name(mask_channel) },
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
    return write_file_atomic(p_path, ss.str()) ? OK : FAILED;
}

Error LMaterial::load_cooked(const std::filesystem::path& p_path)
{
    using enum Error;
    LAssetHeader header{};
    std::vector<uint8_t> payload;
    if (!read_cooked_asset(p_path, AssetType::MATERIAL, header, payload) || payload.size() != sizeof(LMaterialPayloadHeader)) return FAILED;

    LMaterialPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    if (ph.alpha_mode > (uint8_t)AlphaMode::BLEND || ph.mask_channel > (uint8_t)TextureChannel::A) return FAILED;

    *this = LMaterial{};
    guid = header.guid;
    alpha_mode = (AlphaMode)ph.alpha_mode;
    two_sided = ph.two_sided != 0;
    alpha_cutoff = ph.alpha_cutoff;
    color = glm::vec3(ph.color[0], ph.color[1], ph.color[2]);
    base_color_texture = ph.base_color_texture;
    normal_texture = ph.normal_texture;
    mask_texture = ph.mask_texture;
    mask_channel = (TextureChannel)ph.mask_channel;
    return OK;
}

Error LMaterial::save_cooked(const std::filesystem::path& p_path) const
{
    using enum Error;
    LMaterialPayloadHeader ph{};
    ph.base_color_texture = base_color_texture;
    ph.normal_texture = normal_texture;
    ph.mask_texture = mask_texture;
    ph.alpha_cutoff = alpha_cutoff;
    ph.color[0] = color.x;
    ph.color[1] = color.y;
    ph.color[2] = color.z;
    ph.alpha_mode = (uint8_t)alpha_mode;
    ph.two_sided = two_sided ? 1 : 0;
    ph.mask_channel = (uint8_t)mask_channel;
    return write_cooked_asset(p_path, AssetType::MATERIAL, guid, &ph, sizeof(ph)) ? OK : FAILED;
}

}
