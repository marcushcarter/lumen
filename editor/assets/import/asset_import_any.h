#pragma once
#include <filesystem>

namespace lumen {

struct EditorContext;

bool is_importable(const std::filesystem::path& p_src);

bool asset_import_any(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst);

}