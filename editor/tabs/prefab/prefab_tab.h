#pragma once
#include <core/world/prefab.h>
#include <editor/tabs/asset_tab.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <memory>

namespace lumen {

struct DetailsPanel;
struct TabToolbar;

struct PrefabTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::PREFAB;
    static constexpr const char* ICON = ICON_FA_BOX;
    static constexpr double DIRTY_POLL_S = 0.25;

    EditorSelection selection;
    DetailsPanel* details = nullptr;
    Guid root;
    uint64_t selection_version = 0;
    uint64_t _hash = 0;
    uint64_t _saved_hash = 0;
    double _hash_poll = -1.0e9;
    bool _framed = false;
    uint32_t _frame_attempts = 0;

    Error _load(EditorContext& ctx);
    void _fix_roots();
    void _frame(EditorContext& ctx);
    void _draw_toolbar(EditorContext& ctx) override;
    void reload(EditorContext& ctx);

    static void prefab_changed(EditorContext& ctx, Guid p_prefab, std::unique_ptr<Prefab> p_old, const PrefabTab* p_source);

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_prefab; }
    const char* title() const override { return _title.empty() ? "Prefab" : _title.c_str(); }
    bool dirty() const override { return _hash != _saved_hash; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

Entity editor_spawn_prefab(EditorContext& ctx, Guid p_prefab, Entity p_parent);
bool editor_create_prefab(EditorContext& ctx, const std::vector<Entity>& p_tops);
void editor_revert_prefab(EditorContext& ctx, Entity p_entity);
void editor_unpack_prefab(EditorContext& ctx, Entity p_entity);
void editor_open_prefab(EditorContext& ctx, Guid p_prefab);

}
