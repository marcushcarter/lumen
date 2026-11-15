#pragma once
#include <editor/layout/panel.h>
#include <core/scene/scene.h>
#include <editor/documents/scene/editor_spawn.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorSelection;

struct ScenePanel : Panel
{
    static constexpr const char* PAYLOAD = "LUMEN_OUTLINER";
    static constexpr uint32_t NONE = 0xFFFFFFFF;
    static constexpr double WORLD_REFRESH_INTERVAL = 0.25;

    struct Row {
        Entity entity;
        uint16_t depth;
        bool has_children;
    };

    struct Node {
        Entity entity;
        uint32_t parent;
        uint32_t first_child;
        uint32_t child_count;
        uint32_t subtree;
        uint32_t subtree_hidden;
        uint32_t matches;
        bool folder;
        bool match;
    };

    struct DragPayload { uint32_t count; };

    std::vector<Row> rows;
    std::vector<Node> nodes;
    std::vector<uint32_t> order;
    std::vector<uint32_t> roots;
    std::vector<uint32_t> node_of;
    uint64_t world_version = UINT64_MAX;
    uint64_t entity_version = UINT64_MAX;
    double last_rebuild = -1.0;
    bool dirty = true;
    bool expect_world_change = false;

    char filter[128] = {};
    bool sort_ascending = true;

    Entity last_primary = ENTITY_NULL;
    Entity anchor = ENTITY_NULL;
    bool scroll_to_selected = false;

    Entity rename_entity = ENTITY_NULL;
    char rename_buffer[64] = {};
    bool rename_focus = false;

    void _refresh(const Scene& world);
    void _rebuild(const Scene& world);
    void _emit(const Scene& world, uint32_t p_node, uint16_t p_depth);
    const Node* _node(Entity p_entity) const;

    void _draw_toolbar(EditorContext& ctx);
    void _draw_table(EditorContext& ctx, float p_footer_h);
    void _draw_row(EditorContext& ctx, int p_index, float p_row_h);
    void _draw_footer(EditorContext& ctx);
    void _row_menu(EditorContext& ctx, const Row& p_row);

    int _row_find(Entity p_entity) const;
    void _click(EditorContext& ctx, int p_index, bool p_ctrl, bool p_shift);
    void _select_only(EditorContext& ctx, int p_index);
    void _clear_selection(EditorContext& ctx);

    void _reveal(EditorContext& ctx, Entity p_entity);
    void _set_expanded(Scene& world, Entity p_entity, bool p_expanded);
    std::vector<Entity> _top_selection(const EditorContext& ctx) const;
    void _new_folder(EditorContext& ctx);
    void _add(EditorContext& ctx, SpawnType p_type);
    void _delete_selection(EditorContext& ctx);
    void _delete_now(EditorContext& ctx, bool p_keep_children);
    bool _can_drop(const EditorContext& ctx, Entity p_target) const;
    void _asset_drop(EditorContext& ctx, Entity p_target);
    void _drop(EditorContext& ctx, Entity p_target);
    void _set_hidden(Scene& world, Entity p_entity, bool p_hidden);
    void _set_hidden_selection(EditorContext& ctx, bool p_hidden);

    void _rename_begin(const Scene& world, Entity p_entity);
    void _rename_commit(EditorContext& ctx);
    void _rename_end();

    const char* name() const override { return "Scene"; }
    const char* icon() const override { return ICON_FA_SITEMAP; }

    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    ImVec2 window_padding() const override { return ImVec2(0.0f, 0.0f); }
    void draw_contents(EditorContext& ctx) override;
};

}
