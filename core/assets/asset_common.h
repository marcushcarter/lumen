#pragma once
#include <core/assets/guid.h>
#include <cstdint>
#include <string_view>
#include <filesystem>

namespace lumen {

enum class AssetType : uint32_t {
    NONE = 0,
    TEXTURE,
    MESH,
};

inline constexpr uint32_t LASSET_VERSION = 1;

inline AssetType asset_type_from_u32(uint32_t p_v) {
    switch (p_v) {
        case static_cast<uint32_t>(AssetType::TEXTURE):  return AssetType::TEXTURE;
        case static_cast<uint32_t>(AssetType::MESH):  return AssetType::MESH;
        default: return AssetType::NONE;
    }
}

inline std::string_view asset_type_section(AssetType p_type) {
    switch (p_type) {
        case AssetType::TEXTURE: return "texture";
        case AssetType::MESH: return "mesh";
        default: return "None";
    }
}

struct AssetInfo {
    uint32_t version = 0;
    Guid guid{};
    AssetType type = AssetType::NONE;
    bool valid() const { return version != 0 && type != AssetType::NONE; }
};

inline constexpr uint32_t BCON_MAGIC = uint32_t('B') | (uint32_t('C') << 8) | (uint32_t('O') << 16) | (uint32_t('N') << 24);

struct LAssetHeader {
    uint32_t magic;
    uint32_t version;
    Guid guid;
    AssetType type;
    uint32_t payload_size;
};
static_assert(sizeof(Guid) == 8, "header assumes 8-byte Guid");
static_assert(sizeof(LAssetHeader) == 24, "LAssetHeader layout changed");

AssetInfo read_asset_info(const std::filesystem::path& p_path);
bool read_asset_header(const std::filesystem::path& p_path, LAssetHeader& r_header);

}