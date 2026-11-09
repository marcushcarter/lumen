#pragma once
#include <editor/assets/import/model_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <editor/assets/import/reimport_kind.h>
#include <core/base/error.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct EditorContext;

struct ReimportTarget
{
    ReimportKind kind = ReimportKind::NONE;
    std::filesystem::path asset;
};

struct ReimportSettings
{
    std::filesystem::path source;
    ModelCooker::Settings model;
    TextureCooker::CookSettings texture;
};

ReimportTarget reimport_target(const std::filesystem::path& p_asset);
bool can_reimport(const std::filesystem::path& p_asset);
bool reimport_has_settings(ReimportKind p_kind);
bool reimport_load_settings(const ReimportTarget& p_target, ReimportSettings& r_settings);
Error reimport_asset(EditorContext& ctx, const std::filesystem::path& p_asset, const ReimportSettings* p_override = nullptr);
uint32_t reimport_assets(EditorContext& ctx, const std::vector<std::filesystem::path>& p_assets);
void reimport_with_settings(EditorContext& ctx, const std::vector<std::filesystem::path>& p_assets);

}
