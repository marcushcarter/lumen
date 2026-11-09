#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <filesystem>
#include <string>
#include <vector>

namespace lumen {

struct EditorContext;

struct AssetReference
{
    enum class Kind : uint8_t { ASSET, WORLD, OPEN_WORLD, PROJECT };

    Kind kind = Kind::ASSET;
    std::filesystem::path path;
    Guid guid;
    AssetType type = AssetType::NONE;
    std::string name;
    uint32_t count = 0;
};

struct ReplaceResult
{
    uint32_t updated = 0;
    uint32_t failed = 0;
};

std::vector<AssetReference> find_asset_references(EditorContext& ctx, Guid p_target);
std::vector<std::string> replace_reference_warnings(EditorContext& ctx, Guid p_from, Guid p_to);
bool confirm_reference_tabs(EditorContext& ctx, const std::vector<AssetReference>& p_refs);
ReplaceResult replace_asset_references(EditorContext& ctx, Guid p_from, Guid p_to, const std::vector<AssetReference>& p_refs);
bool delete_replaced_asset(EditorContext& ctx, Guid p_guid);

}
