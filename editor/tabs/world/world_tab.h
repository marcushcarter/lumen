#pragma once
#include <core/base/error.h>
#include <editor/application/editor_context.h>
#include <editor/tabs/editor_tab.h>
#include <editor/layout/editor_layout.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <string>

namespace lumen {

struct SharedPanels;
struct DetailsPanel;

struct WorldTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::WORLD;
    static constexpr int VERSION = 2;
    static constexpr const char* ICON = ICON_FA_EARTH_AMERICAS;
    static constexpr double DIRTY_POLL_S = 0.25;

    EditorLayout layout;
    DetailsPanel* details = nullptr;
    uint64_t selection_version = 0;
    std::string _title;
    std::string _tooltip;
    Guid _pending;
    bool _has_pending = false;
    uint64_t _hash = 0;
    uint64_t _saved_hash = 0;
    double _hash_poll = -1.0e9;

    const std::filesystem::path* _path(EditorContext& ctx) const;
    uint64_t _compute_hash(EditorContext& ctx) const;
    bool _confirm_discard() const;
    void _load(EditorContext& ctx, Guid p_world);
    void _unload(EditorContext& ctx);
    void _draw_empty(ImVec2 p_min, ImVec2 p_max);
    void _draw_toolbar(EditorContext& ctx);
    float _draw_play_controls(EditorContext& ctx, ImVec2 p_bar_min, ImVec2 p_bar_max);

    Error initialize(SharedPanels& r_shared);
    void shutdown();
    void draw_menu();
    void open(Guid p_world);
    void show(EditorContext& ctx, Guid p_world);
    void reset();
    bool has_world() const { return guid.valid(); }
    Error write(EditorContext& ctx, const std::filesystem::path& p_path) const;

    const char* title() const override { return _title.empty() ? ICON_FA_EARTH_AMERICAS "  World" : _title.c_str(); }
    ImU32 icon_color() const override { return has_world() ? ui_style().color.asset_world : ui_style().color.text_dim; }
    const char* tooltip() const override { return _tooltip.empty() ? nullptr : _tooltip.c_str(); }
    bool dirty() const override { return has_world() && _hash != _saved_hash; }
    Error save(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
