#pragma once
#include <core/assets/asset_common.h>
#include <filesystem>
#include <string>
#include <cstring>

namespace lumen {

struct AssetDragPayload
{
    static constexpr const char* TYPE = "LUMEN_ASSET";
    static constexpr uint32_t MAX_PATH_LENGTH = 1024;

    Guid guid;
    AssetType type = AssetType::NONE;
    char path[MAX_PATH_LENGTH] = {};

    static AssetDragPayload make(const std::filesystem::path& p_path) {
        AssetDragPayload p;
        const AssetInfo info = read_asset_info(p_path);
        if (info.valid()) {
            p.guid = info.guid;
            p.type = info.type;
        }
        const std::string s = p_path.string();
        if (s.size() < MAX_PATH_LENGTH) std::memcpy(p.path, s.c_str(), s.size() + 1);
        return p;
    }
};

static_assert(std::is_trivially_copyable_v<AssetDragPayload>);

}