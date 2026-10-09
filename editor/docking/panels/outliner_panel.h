#pragma once
#include <editor/docking/panel.h>
#include <core/world/world.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorFolders;
struct EditorSelection;

struct OutlinerPanel : Panel
{
    static constexpr const char* PAYLOAD = "LUMEN_OUTLINER";
    static constexpr uint32_t NONE = 0xFFFFFFFF;
    static constexpr double WORLD_REFRESH_INTERVAL = 0.25;
    static constexpr float PAD_X = 10.0f;

    enum class RowKind : uint8_t { FOLDER, ENTITY };
    enum class SortColumn : uint8_t { LABEL, TYPE };

    struct Row
    {
        Entity entity;
        uint32_t folder;
        uint16_t depth;
        RowKind kind;
    };

    struct Item
    {
        uint32_t slot;
        Entity entity;
        const char* name;
        const char* type;
        bool match;
    };

    struct DragPayload { uint32_t count; };

    std::vector<Row> rows;
    std::vector<Item> items;
    std::vector<uint32_t> item_begin;
    std::vector<uint32_t> folder_order;
    std::vector<uint32_t> folder_begin;
    std::vector<uint32_t> folder_total;
    std::vector<uint32_t> folder_hidden;
    std::vector<uint32_t> folder_matches;
    uint64_t world_version = UINT64_MAX;
    uint64_t entity_version = UINT64_MAX;
    uint64_t folders_version = UINT64_MAX;
    double last_rebuild = -1.0;
    bool dirty = true;
    bool expect_world_change = false;

    char filter[128] = {};
    SortColumn sort_column = SortColumn::LABEL;
    bool sort_ascending = true;

    Entity last_primary = ENTITY_NULL;
    Row anchor = { ENTITY_NULL, NONE, 0, RowKind::ENTITY };
    bool has_anchor = false;
    bool scroll_to_selected = false;

    RowKind rename_kind = RowKind::ENTITY;
    uint32_t rename_folder = NONE;
    Entity rename_entity = ENTITY_NULL;
    char rename_buffer[64] = {};
    bool rename_focus = false;

    const char* name() const override { return ICON_FA_IMAGE "  Outliner"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    ImVec2 window_padding() const override { return ImVec2(0.0f, 8.0f); }
    void draw_contents(EditorContext& ctx) override;

    void _refresh(const World& world, const EditorFolders& folders);
    void _rebuild(const World& world, const EditorFolders& folders, bool p_full);
    void _emit(const EditorFolders& folders, uint32_t p_slot, uint16_t p_depth);
    uint32_t _slot(const EditorFolders& folders, uint32_t p_folder) const;

    void _draw_toolbar(EditorContext& ctx);
    void _draw_table(EditorContext& ctx, float p_footer_h);
    void _draw_row(EditorContext& ctx, int p_index, float p_row_h);
    void _draw_footer(EditorContext& ctx);
    void _row_menu(EditorContext& ctx, const Row& p_row);

    bool _row_selected(const EditorSelection& selection, const Row& p_row) const;
    void _row_add(EditorSelection& selection, const Row& p_row) const;
    int _row_find(const Row& p_row) const;
    void _click(EditorContext& ctx, int p_index, bool p_ctrl, bool p_shift);
    void _select_only(EditorContext& ctx, int p_index);
    void _clear_selection(EditorContext& ctx);

    void _reveal(EditorContext& ctx, Entity p_entity);
    void _new_folder(EditorContext& ctx);
    void _delete_selection(EditorContext& ctx);
    bool _can_drop(const EditorContext& ctx, uint32_t p_target) const;
    void _drop(EditorContext& ctx, uint32_t p_target);
    void _set_hidden(World& world, const EditorFolders& folders, uint32_t p_slot, bool p_hidden);
    void _set_hidden_selection(EditorContext& ctx, bool p_hidden);

    void _rename_begin_entity(const World& world, Entity p_entity);
    void _rename_begin_folder(const EditorFolders& folders, uint32_t p_folder);
    void _rename_commit(EditorContext& ctx);
    void _rename_end();
    bool _renaming(const Row& p_row) const;
};

}
