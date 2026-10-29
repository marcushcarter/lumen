#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/io/file_io.h>
#include <toml++/toml.hpp>
#include <cstring>
#include <fstream>

namespace lumen {

AssetInfo read_asset_info(const std::filesystem::path& p_path)
{
    AssetInfo info;

    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return info; }

    auto asset = tbl["asset"];
    info.version = static_cast<uint32_t>(asset["version"].value_or<int64_t>(0));
    if (auto g = asset["guid"].value<std::string>()) info.guid = Guid::from_string(*g);
    info.type = asset_type_from_u32(static_cast<uint32_t>(asset["type"].value_or<int64_t>(0)));

    return info;
}

bool read_asset_header(const std::filesystem::path& p_path, LAssetHeader& r_header)
{
    std::ifstream f(p_path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&r_header), sizeof(r_header));
    if (!f || f.gcount() != (std::streamsize)sizeof(r_header)) return false;
    return r_header.magic == BCON_MAGIC && asset_version_supported(r_header.type, r_header.version);
}

bool asset_version_supported(AssetType p_type, uint32_t p_version)
{
    switch (p_type) {
        case AssetType::MESH: return p_version == LMESH_VERSION || p_version == LMESH_VERSION_SLOT_TABLE || p_version == LMESH_VERSION_INTERLEAVED;
        case AssetType::TEXTURE: return p_version == LASSET_VERSION;
        case AssetType::MODEL: return p_version == LASSET_VERSION;
        case AssetType::MATERIAL: return p_version == LASSET_VERSION;
        default: return false;
    }
}

bool write_cooked_asset(const std::filesystem::path& p_path, AssetType p_type, Guid p_guid, const void* p_payload, size_t p_size)
{
    if (p_size > COOKED_PAYLOAD_MAX) return false;
    const LAssetHeader header{ BCON_MAGIC, LASSET_VERSION, p_guid, p_type, (uint32_t)p_size };
    std::vector<uint8_t> bytes(sizeof(header) + p_size);
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (p_size) std::memcpy(bytes.data() + sizeof(header), p_payload, p_size);

    std::error_code ec;
    std::filesystem::create_directories(p_path.parent_path(), ec);
    return write_file_atomic(p_path, bytes.data(), bytes.size());
}

bool read_cooked_asset(const std::filesystem::path& p_path, AssetType p_type, LAssetHeader& r_header, std::vector<uint8_t>& r_payload)
{
    std::ifstream f(p_path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&r_header), sizeof(r_header));
    if (!f || r_header.magic != BCON_MAGIC || r_header.type != p_type || !asset_version_supported(r_header.type, r_header.version)) return false;
    if (r_header.payload_size > COOKED_PAYLOAD_MAX) return false;
    r_payload.resize(r_header.payload_size);
    if (r_header.payload_size) f.read(reinterpret_cast<char*>(r_payload.data()), (std::streamsize)r_header.payload_size);
    return (bool)f;
}

}
