#include <editor/assets/import/reimport.h>
#include <editor/assets/import/model_cooker.h>
#include <editor/assets/import/animation_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <editor/application/editor_context.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/tabs/tab_manager.h>
#include <editor/assets/import/import_dialog.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/windows/window_driver_win32.h>
#include <string>
#include <string_view>
#include <core/assets/asset_common.h>
#include <core/assets/lmodel.h>
#include <algorithm>

namespace lumen {

ReimportTarget reimport_target(const std::filesystem::path& p_asset)
{
    const AssetInfo info = read_asset_info(p_asset);
    if (!info.valid()) return {};

    switch (info.type) {
        case AssetType::MODEL: {
            LModel model;
            if (model.load(p_asset) != Error::OK || model.import.source.empty()) return {};
            return { ReimportKind::MODEL, p_asset };
        }
        case AssetType::MESH: {
            MeshCooker::CookSettings settings;
            std::filesystem::path source;
            if (!MeshCooker::load_settings(p_asset, settings, source)) return {};
            return { ReimportKind::MESH, p_asset };
        }
        case AssetType::TEXTURE: {
            TextureCooker::CookSettings settings;
            std::filesystem::path source;
            if (!TextureCooker::load_settings(p_asset, settings, source)) return {};
            return { ReimportKind::TEXTURE, p_asset };
        }
        case AssetType::ANIMATION: {
            AnimationCooker::Record record;
            if (!AnimationCooker::load_record(p_asset, record)) return {};
            return { ReimportKind::ANIMATION, p_asset };
        }
        default:
            return {};
    }
}

bool can_reimport(const std::filesystem::path& p_asset)
{
    return reimport_target(p_asset).kind != ReimportKind::NONE;
}

bool reimport_has_settings(ReimportKind p_kind)
{
    return p_kind == ReimportKind::MODEL || p_kind == ReimportKind::MESH || p_kind == ReimportKind::TEXTURE;
}

bool reimport_load_settings(const ReimportTarget& p_target, ReimportSettings& r_settings)
{
    switch (p_target.kind) {
        case ReimportKind::MODEL: {
            LModel model;
            if (model.load(p_target.asset) != Error::OK || model.import.source.empty()) return false;
            r_settings.source = std::filesystem::path(model.import.source);
            r_settings.model.mode = model.import.mode;
            const std::filesystem::path lmesh = model.mesh.valid() ? ModelCooker::find_mesh(p_target.asset.parent_path(), model.mesh) : std::filesystem::path();
            std::filesystem::path mesh_source;
            if (!lmesh.empty()) MeshCooker::load_settings(lmesh, r_settings.model.mesh_settings, mesh_source);
            return true;
        }
        case ReimportKind::MESH:
            return MeshCooker::load_settings(p_target.asset, r_settings.model.mesh_settings, r_settings.source);
        case ReimportKind::TEXTURE:
            return TextureCooker::load_settings(p_target.asset, r_settings.texture, r_settings.source);
        default:
            return false;
    }
}

static Error _reimport_texture(EditorContext& ctx, const std::filesystem::path& p_ltexture, const TextureCooker::CookSettings* p_override)
{
    TextureCooker::CookSettings settings;
    std::filesystem::path source;
    if (!TextureCooker::load_settings(p_ltexture, settings, source)) return Error::FAILED;
    if (p_override) settings = *p_override;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        log_write("Reimport failed: source '%s' no longer exists.", source.string().c_str());
        return Error::FAILED;
    }
    TextureCooker::import_async(ctx, source, p_ltexture, settings);
    return Error::OK;
}

static std::filesystem::path _scope(const ReimportTarget& p_target)
{
    return p_target.kind == ReimportKind::MODEL ? p_target.asset.parent_path() : p_target.asset;
}

static bool _confirm_reload(const EditorTab& p_tab)
{
    std::string_view name = p_tab.title();
    if (const size_t gap = name.find("  "); gap != std::string_view::npos) name.remove_prefix(gap + 2);
    const std::wstring heading = L"'" + drivers::WindowDriverWin32::utf8_to_wide(name) + L"' has unsaved changes";
    return drivers::Win32Dialogs::ask(L"Lumen Editor", heading.c_str(), L"Reimporting reloads it from the new files and discards those changes.", L"Reimport", L"Cancel");
}

static bool _confirm_tabs(EditorContext& ctx, const std::vector<ReimportTarget>& p_targets)
{
    if (!ctx.tabs) return true;
    for (EditorTab* t : ctx.tabs->tabs) {
        const std::filesystem::path* path = t->asset_path();
        if (!path || !t->dirty() || t->kind == TabKind::MATERIAL) continue;
        for (const ReimportTarget& target : p_targets) {
            const std::filesystem::path scope = _scope(target);
            if (*path != scope && !AssetImportTracker::path_contains(scope, *path)) continue;
            if (!_confirm_reload(*t)) return false;
            break;
        }
    }
    return true;
}

static Error _run(EditorContext& ctx, const ReimportTarget& p_target, const ReimportSettings* p_override = nullptr)
{
    switch (p_target.kind) {
        case ReimportKind::MODEL: return ModelCooker::reimport(ctx, p_target.asset, p_override ? &p_override->model : nullptr);
        case ReimportKind::MESH: return ModelCooker::reimport_mesh(ctx, p_target.asset, p_override ? &p_override->model.mesh_settings : nullptr);
        case ReimportKind::TEXTURE: return _reimport_texture(ctx, p_target.asset, p_override ? &p_override->texture : nullptr);
        case ReimportKind::ANIMATION: return AnimationCooker::reimport(ctx, p_target.asset);
        default: return Error::FAILED;
    }
}

Error reimport_asset(EditorContext& ctx, const std::filesystem::path& p_asset, const ReimportSettings* p_override)
{
    if (!ctx.project || !ctx.imports) return Error::FAILED;
    const ReimportTarget target = reimport_target(p_asset);
    if (target.kind == ReimportKind::NONE) {
        log_write("Reimport: '%s' has no import source.", p_asset.filename().string().c_str());
        return Error::FAILED;
    }
    if (!_confirm_tabs(ctx, { target })) return Error::CANCELED;
    return _run(ctx, target, p_override);
}

uint32_t reimport_assets(EditorContext& ctx, const std::vector<std::filesystem::path>& p_assets)
{
    if (!ctx.project || !ctx.imports) return 0;
    std::vector<ReimportTarget> targets;
    for (const std::filesystem::path& p : p_assets) {
        ReimportTarget t = reimport_target(p);
        if (t.kind == ReimportKind::NONE) continue;
        const bool seen = std::any_of(targets.begin(), targets.end(), [&](const ReimportTarget& o) { return o.asset == t.asset; });
        if (!seen) targets.push_back(std::move(t));
    }
    if (targets.empty() || !_confirm_tabs(ctx, targets)) return 0;
    uint32_t started = 0;
    for (const ReimportTarget& t : targets) started += _run(ctx, t) == Error::OK ? 1u : 0u;
    return started;
}

void reimport_with_settings(EditorContext& ctx, const std::vector<std::filesystem::path>& p_assets)
{
    std::vector<std::filesystem::path> quick;
    std::vector<std::filesystem::path> queued;
    for (const std::filesystem::path& p : p_assets) {
        const ReimportTarget t = reimport_target(p);
        if (t.kind == ReimportKind::NONE) continue;
        if (!reimport_has_settings(t.kind) || !ctx.import_dialog) {
            quick.push_back(p);
            continue;
        }
        if (std::find(queued.begin(), queued.end(), t.asset) != queued.end()) continue;
        if (ctx.import_dialog->push_reimport(t)) queued.push_back(t.asset);
    }
    if (!quick.empty()) reimport_assets(ctx, quick);
}

}
