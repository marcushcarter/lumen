#include <editor/assets/asset_references.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/import/icon_cooker.h>
#include <editor/assets/import/splash_cooker.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/world_tab.h>
#include <core/assets/lanim.h>
#include <core/assets/lmesh.h>
#include <core/io/file_io.h>
#include <core/io/path.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/archive.h>
#include <core/world/world.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/windows/window_driver_win32.h>
#include <toml++/toml.hpp>
#include <sstream>
#include <string_view>

namespace lumen {

static uint32_t _walk(toml::node& r_node, const std::string& p_from, const std::string* p_to)
{
    uint32_t count = 0;
    if (toml::value<std::string>* s = r_node.as_string()) {
        if (s->get() != p_from) return 0;
        if (p_to) s->get() = *p_to;
        return 1;
    }
    if (toml::table* t = r_node.as_table()) {
        for (auto&& [key, child] : *t) count += _walk(child, p_from, p_to);
        return count;
    }
    if (toml::array* a = r_node.as_array()) {
        for (toml::node& child : *a) count += _walk(child, p_from, p_to);
    }
    return count;
}

static uint32_t _walk_file(toml::table& r_table, const std::string& p_from, const std::string* p_to)
{
    uint32_t count = 0;
    for (auto&& [key, child] : r_table) {
        if (key.str() == "asset") continue;
        count += _walk(child, p_from, p_to);
    }
    return count;
}

struct GuidSwapArchive : Archive
{
    Guid from;
    Guid to;
    bool apply = false;
    uint32_t count = 0;

    void field(const char*, bool&) override {}
    void field(const char*, int32_t&) override {}
    void field(const char*, uint32_t&) override {}
    void field(const char*, float&) override {}
    void field(const char*, vec2&) override {}
    void field(const char*, vec3&) override {}
    void field(const char*, vec4&) override {}
    void field(const char*, uvec3&) override {}
    void field(const char*, quat&) override {}
    void field(const char*, std::string&) override {}
    void field(const char*, char*, size_t) override {}
    void field(const char*, Guid& r_value) override {
        if (r_value != from) return;
        count++;
        if (apply) r_value = to;
    }
};

static uint32_t _swap_world(World& r_world, Guid p_from, Guid p_to, bool p_apply)
{
    GuidSwapArchive archive;
    archive.from = p_from;
    archive.to = p_to;
    archive.apply = p_apply;
    std::vector<Entity> touched;
    r_world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
        const uint32_t before = archive.count;
        for (const ComponentInfo& info : r_world.components) {
            if (info.serialize) info.serialize(r_world, p_entity, archive);
        }
        if (archive.count != before) touched.push_back(p_entity);
    });
    if (p_apply) {
        for (Entity e : touched) r_world.touch(e);
    }
    return archive.count;
}

static const std::filesystem::path* _open_world_path(const EditorContext& ctx)
{
    if (!ctx.world_tab || !ctx.world_tab->has_world() || !ctx.assets || !ctx.world) return nullptr;
    const AssetRegistry::Entry* e = ctx.assets->find(ctx.world_tab->guid);
    return e ? &e->path : nullptr;
}

std::vector<AssetReference> find_asset_references(EditorContext& ctx, Guid p_target)
{
    std::vector<AssetReference> refs;
    if (!ctx.assets || !p_target.valid()) return refs;
    const std::string target = p_target.to_string();
    const std::filesystem::path* open_world = _open_world_path(ctx);

    for (const auto& [guid, entry] : ctx.assets->entries) {
        if (guid == p_target) continue;
        if (open_world && entry.path == *open_world) {
            const uint32_t count = _swap_world(*ctx.world, p_target, {}, false);
            if (count) refs.push_back({ AssetReference::Kind::OPEN_WORLD, entry.path, guid, entry.type, entry.name, count });
            continue;
        }
        toml::table tbl;
        try { tbl = toml::parse_file(entry.path.string()); }
        catch (const toml::parse_error&) { continue; }
        const uint32_t count = _walk_file(tbl, target, nullptr);
        if (!count) continue;
        const AssetReference::Kind kind = entry.type == AssetType::WORLD ? AssetReference::Kind::WORLD : AssetReference::Kind::ASSET;
        refs.push_back({ kind, entry.path, guid, entry.type, entry.name, count });
    }

    if (ctx.project) {
        const uint32_t count = (ctx.project->settings.window_icon == p_target ? 1u : 0u) + (ctx.project->settings.splash_image == p_target ? 1u : 0u);
        if (count) refs.push_back({ AssetReference::Kind::PROJECT, ctx.project->root, {}, AssetType::NONE, "Project Settings", count });
    }
    return refs;
}

static toml::table _record(const std::filesystem::path& p_path)
{
    try { return toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return {}; }
}

static Guid _record_guid(const toml::table& p_table, AssetType p_type, const char* p_key)
{
    return Guid::from_string(p_table[asset_type_section(p_type)][p_key].value_or(std::string()));
}

std::vector<std::string> replace_reference_warnings(EditorContext& ctx, Guid p_from, Guid p_to)
{
    std::vector<std::string> warnings;
    if (!ctx.assets || !p_from.valid() || !p_to.valid()) return warnings;
    const AssetRegistry::Entry* from = ctx.assets->find(p_from);
    const AssetRegistry::Entry* to = ctx.assets->find(p_to);
    if (!from || !to) return warnings;
    if (from->type != to->type) {
        warnings.push_back("The replacement is a different type of asset.");
        return warnings;
    }

    const toml::table a = _record(from->path);
    const toml::table b = _record(to->path);
    switch (from->type) {
        case AssetType::ANIMATION:
            if (_record_guid(a, AssetType::ANIMATION, "skeleton") != _record_guid(b, AssetType::ANIMATION, "skeleton")) warnings.push_back("The replacement animation uses a different skeleton.");
            break;
        case AssetType::MESH: {
            if (_record_guid(a, AssetType::MESH, "skeleton") != _record_guid(b, AssetType::MESH, "skeleton")) warnings.push_back("The replacement mesh uses a different skeleton.");
            LMeshStats sa, sb;
            if (ctx.project && lmesh_read_stats(ctx.project->content_path(p_from), sa) && lmesh_read_stats(ctx.project->content_path(p_to), sb) && sa.slots != sb.slots) {
                warnings.push_back("The replacement mesh has " + std::to_string(sb.slots) + " material slots instead of " + std::to_string(sa.slots) + ".");
            }
            break;
        }
        case AssetType::SKELETON:
            if (a[asset_type_section(AssetType::SKELETON)]["fingerprint"].value_or(std::string()) != b[asset_type_section(AssetType::SKELETON)]["fingerprint"].value_or(std::string())) warnings.push_back("The replacement skeleton's bones don't match.");
            break;
        default:
            break;
    }
    return warnings;
}

bool confirm_reference_tabs(EditorContext& ctx, const std::vector<AssetReference>& p_refs)
{
    if (!ctx.tabs) return true;
    for (EditorTab* t : ctx.tabs->tabs) {
        const std::filesystem::path* path = t->asset_path();
        if (!path || !t->dirty()) continue;
        for (const AssetReference& r : p_refs) {
            if (r.kind != AssetReference::Kind::ASSET || r.path != *path) continue;
            std::string_view name = t->title();
            if (const size_t gap = name.find("  "); gap != std::string_view::npos) name.remove_prefix(gap + 2);
            const std::wstring heading = L"'" + drivers::WindowDriverWin32::utf8_to_wide(name) + L"' has unsaved changes";
            if (!drivers::Win32Dialogs::ask(L"Lumen Editor", heading.c_str(), L"Replacing references reloads it from disk and discards those changes.", L"Replace", L"Cancel")) return false;
            break;
        }
    }
    return true;
}

static void _update_binary(EditorContext& ctx, const AssetReference& p_ref, const toml::table& p_table, Guid p_to)
{
    if (!ctx.project) return;
    const std::filesystem::path bin = ctx.project->content_path(p_ref.guid);
    if (p_ref.type == AssetType::MESH && _record_guid(p_table, AssetType::MESH, "skeleton") == p_to) {
        if (!lmesh_set_skeleton(bin, p_to)) log_write("References: couldn't update the skeleton inside '%s'.", p_ref.path.filename().string().c_str());
        if (ctx.renderer) ctx.renderer->geometry.set_skeleton(p_ref.guid, p_to);
    }
    if (p_ref.type == AssetType::ANIMATION && _record_guid(p_table, AssetType::ANIMATION, "skeleton") == p_to) {
        LAnimation clip;
        if (clip.load_cooked(bin) == Error::OK) {
            clip.skeleton = p_to;
            if (clip.save_cooked(bin) != Error::OK) log_write("References: couldn't update the skeleton inside '%s'.", p_ref.path.filename().string().c_str());
        }
    }
}

static void _reload(EditorContext& ctx, const AssetReference& p_ref)
{
    if (!ctx.renderer) return;
    if (p_ref.type == AssetType::MODEL) ctx.renderer->models.load(p_ref.path);
    if (p_ref.type == AssetType::MATERIAL) ctx.renderer->materials.load(p_ref.path);
}

ReplaceResult replace_asset_references(EditorContext& ctx, Guid p_from, Guid p_to, const std::vector<AssetReference>& p_refs)
{
    ReplaceResult result;
    if (!p_from.valid() || !p_to.valid() || p_from == p_to) return result;
    const std::string from = p_from.to_string();
    const std::string to = p_to.to_string();
    std::vector<std::filesystem::path> reload_tabs;

    for (const AssetReference& r : p_refs) {
        switch (r.kind) {
            case AssetReference::Kind::OPEN_WORLD:
                if (ctx.world && _swap_world(*ctx.world, p_from, p_to, true)) result.updated++;
                break;
            case AssetReference::Kind::PROJECT: {
                if (!ctx.project) break;
                ProjectSettings& s = ctx.project->settings;
                const bool icon = s.window_icon == p_from;
                const bool splash = s.splash_image == p_from;
                if (icon) s.window_icon = p_to;
                if (splash) s.splash_image = p_to;
                if (ctx.project->save() != Error::OK) {
                    result.failed++;
                    break;
                }
                if (icon) IconCooker::cook_icon(*ctx.project);
                if (splash) SplashCooker::cook_splash(*ctx.project);
                result.updated++;
                break;
            }
            default: {
                toml::table tbl = _record(r.path);
                if (tbl.empty() || !_walk_file(tbl, from, &to)) {
                    result.failed++;
                    break;
                }
                std::ostringstream ss;
                ss << tbl;
                if (!write_file_atomic(r.path, ss.str())) {
                    log_write("References: couldn't write '%s'.", r.path.string().c_str());
                    result.failed++;
                    break;
                }
                _update_binary(ctx, r, tbl, p_to);
                _reload(ctx, r);
                reload_tabs.push_back(r.path);
                result.updated++;
                break;
            }
        }
    }

    if (!reload_tabs.empty() && ctx.reload_asset_tabs) ctx.reload_asset_tabs(reload_tabs);
    log_write("References: replaced in %u place(s)%s.", result.updated, result.failed ? ", some failed (see above)" : "");
    return result;
}

bool delete_replaced_asset(EditorContext& ctx, Guid p_guid)
{
    if (!ctx.assets || !ctx.project) return false;
    const AssetRegistry::Entry* e = ctx.assets->find(p_guid);
    if (!e) return false;
    const std::filesystem::path path = e->path;
    const AssetType type = e->type;
    if (ctx.tabs) {
        for (EditorTab* t : ctx.tabs->tabs) {
            if (t->guid == p_guid) ctx.tabs->close(t->kind, t->guid, true);
        }
    }
    if (ctx.renderer) {
        switch (type) {
            case AssetType::TEXTURE: ctx.renderer->textures.unload(p_guid); break;
            case AssetType::MESH: ctx.renderer->geometry.unload(p_guid); break;
            case AssetType::MODEL: ctx.renderer->models.unload(p_guid); break;
            case AssetType::MATERIAL: ctx.renderer->materials.unload(p_guid); break;
            case AssetType::SKELETON: ctx.renderer->skeletons.unload(p_guid); break;
            default: break;
        }
    }
    std::error_code ec;
    const std::filesystem::path bin = ctx.project->content_path(p_guid);
    if (std::filesystem::exists(bin, ec)) Paths::remove_to_recycle(bin);
    Paths::remove_to_recycle(path);
    ctx.assets->dirty = true;
    return true;
}

}
