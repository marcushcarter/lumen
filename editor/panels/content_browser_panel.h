#pragma once
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct ContentBrowserPanel : Panel
{
    static constexpr double CACHE_REFRESH_INTERVAL_S = 1.0;

    struct Entry {
        std::filesystem::path path;
        std::string name;
        std::string type;
        std::string lower_name;
        Guid guid;
        const char* icon = nullptr;
        bool is_dir = false;
        bool is_texture = false;
        bool guid_checked = false;
        AssetType asset_type = AssetType::NONE;
    };

    struct Dir {
        std::string path_str;
        std::string name;
        std::vector<std::filesystem::path> subdirs;
        std::vector<Entry> entries;
        bool exists = false;
    };

    struct ItemResult {
        bool clicked = false;
        bool activated = false;
        bool context = false;
    };

    std::unordered_map<std::filesystem::path, Dir> dirs;
    double last_refresh = -1.0e9;
    bool refresh_requested = false;
    AssetRegistry* registry = nullptr;

    std::filesystem::path selected_folder;
    std::filesystem::path _root;
    float split_x = 0.18f;

    char search_buf[256] = {};
    std::vector<std::filesystem::path> selection;
    std::filesystem::path anchor;
    std::filesystem::path _selection_folder;
    bool delete_requested = false;
    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    bool rename_focus = false;
    bool _scroll_to_rename = false;
    std::filesystem::path cancel_request;
    AssetDragPayload drag_payload;
    std::unordered_map<std::filesystem::path, AssetInfo> _thumb_infos;
    std::vector<uint32_t> _visible;

    void _cache_tick(double p_now);
    Dir& _cache_get(const std::filesystem::path& p_dir);
    void _cache_scan(const std::filesystem::path& p_dir, Dir& r_dir);
    void request_refresh() { refresh_requested = true; if (registry) registry->request_rebuild(); }

    void _reset_root(const std::filesystem::path& p_root);
    void _draw_folder_node(const std::filesystem::path& p_dir, int p_depth);

    std::filesystem::path _target_folder(const EditorContext& ctx) const;
    bool _nested_in_selection(const std::filesystem::path& p_path) const;
    bool _is_selected(const std::filesystem::path& p_path) const;
    void _select_only(const std::filesystem::path& p_path);
    void _clear_selection();
    void _click(const Dir& p_dir, uint32_t p_visible, bool p_ctrl, bool p_shift);

    void _begin_new(const std::filesystem::path& p_path);
    void _rename_begin(const std::filesystem::path& p_path);
    void _rename_commit();
    void _new_folder(EditorContext& ctx);
    void _duplicate_selection(EditorContext& ctx);
    void _import(EditorContext& ctx);
    void _move_selection(const std::filesystem::path& p_dst);
    void _drop_target(const std::filesystem::path& p_dst);

    void _delete_content(EditorContext& ctx, const std::filesystem::path& p_asset);
    void _delete_asset(EditorContext& ctx, const std::filesystem::path& p_path);
    void _delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder);
    void _delete_folders_only();
    void _delete_selection();
    void _delete_now(EditorContext& ctx);

    AssetInfo _resolve_asset(const std::filesystem::path& p_path);
    float _row_height() const;
    void _context_menu(EditorContext& ctx, const std::filesystem::path* p_row);
    ItemResult _list_item(EditorContext& ctx, ImTextureID p_texture, const char* p_icon, ImU32 p_icon_col, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress = -1.0f, bool p_importing = false);
    void _list_draw(EditorContext& ctx);

    void _add_asset_menu(EditorContext& ctx);
    void _draw_tree(const std::filesystem::path& p_root, ImVec2 p_origin, float p_width, float p_height);
    void _draw_toolbar(EditorContext& ctx);
    void _draw_footer();

    const char* name() const override { return ICON_FA_BOOK "  Content Browser"; }
    bool in_footer() const override { return true; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;
};

}
