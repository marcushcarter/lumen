#pragma once
#include <core/assets/guid.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lumen {

struct ContentBrowserPanel : Panel
{
    static constexpr double CACHE_REFRESH_INTERVAL_S = 1.0;
    static constexpr size_t MAX_RESULTS = 2000;

    enum class NewAsset : uint8_t { SCENE, TEMPLATE, TERRAIN, MODEL, MATERIAL, BLEND_SPACE, LOCOMOTION, OVERLAY, COUNT };

    struct Entry {
        std::filesystem::path path;
        std::string name;
        std::string lower_name;
        const char* icon = nullptr;
        bool is_dir = false;
    };

    struct Dir {
        std::vector<Entry> entries;
        bool exists = false;
    };

    struct Row {
        const Entry* entry = nullptr;
        std::filesystem::path pending;
        uint16_t depth = 0;
        bool last = false;
        uint64_t lines = 0;
    };

    std::unordered_map<std::filesystem::path, Dir> dirs;
    std::unordered_set<std::filesystem::path> open_folders;
    std::filesystem::path root;
    double last_refresh = -1.0e9;
    bool refresh_requested = false;
    bool root_open = true;
    AssetRegistry* registry = nullptr;

    char search_buf[256] = {};
    std::string _result_query;
    double _result_time = -1.0;
    std::vector<Entry> results;
    std::vector<Row> rows;

    std::vector<std::filesystem::path> selection;
    std::filesystem::path anchor;
    std::vector<std::filesystem::path> _order;
    AssetDragPayload drag_payload;

    bool delete_requested = false;
    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    bool rename_focus = false;
    std::filesystem::path cancel_request;
    std::filesystem::path _scroll_target;
    std::filesystem::path _hover_path;
    AssetInfo _hover_info;

    static const char* icon_for(const std::filesystem::path& p_path, bool p_is_dir);
    static std::string type_name(std::string p_ext);
    static bool open_entry(EditorContext& ctx, const std::filesystem::path& p_path);

    void request_refresh() { refresh_requested = true; if (registry) registry->request_rebuild(); }
    void _reset(const std::filesystem::path& p_root);
    void _cache_tick(double p_now);
    Dir& _cache_get(const std::filesystem::path& p_dir);
    void _cache_scan(const std::filesystem::path& p_dir, Dir& r_dir);
    void _search_collect(const std::filesystem::path& p_dir, const std::string& p_query);
    void _rows_emit(EditorContext& ctx, const std::filesystem::path& p_dir, uint16_t p_depth, uint64_t p_lines);
    void _toggle(const std::filesystem::path& p_dir);

    bool _is_selected(const std::filesystem::path& p_path) const;
    void _select_only(const std::filesystem::path& p_path);
    void _clear_selection();
    void _click(const std::filesystem::path& p_path, bool p_ctrl, bool p_shift);

    void _draw_list(EditorContext& ctx, const char* p_id, ImVec2 p_size, bool p_results);
    bool _draw_row(EditorContext& ctx, const Entry& p_entry, uint16_t p_depth, float p_row_h, bool p_show_path, uint64_t p_lines = 0, bool p_last = true);
    void _draw_pending(EditorContext& ctx, const std::filesystem::path& p_path, uint16_t p_depth, float p_row_h, uint64_t p_lines, bool p_last);
    void _draw_root(float p_row_h);
    void _drag_source(const Entry& p_entry);
    void _list_end(EditorContext& ctx);

    std::filesystem::path _target_folder() const;
    bool _nested_in_selection(const std::filesystem::path& p_path) const;
    void _begin_new(const std::filesystem::path& p_path);
    void _rename_begin(const std::filesystem::path& p_path);
    void _rename_commit();
    void _new_folder(EditorContext& ctx);
    void _duplicate_selection(EditorContext& ctx);
    void _import(EditorContext& ctx);
    void _move_selection(const std::filesystem::path& p_dst);
    void _drop_target(const std::filesystem::path& p_dst);
    void _drop_accept(const std::filesystem::path& p_dst);
    void _delete_content(EditorContext& ctx, const std::filesystem::path& p_asset);
    void _delete_asset(EditorContext& ctx, const std::filesystem::path& p_path);
    void _delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder);
    void _delete_folders_only();
    void _delete_selection();
    void _delete_now(EditorContext& ctx);
    void _context_menu(EditorContext& ctx, const std::filesystem::path& p_row);
    void _create_asset(EditorContext& ctx, NewAsset p_kind);
    void _common_assets_menu(EditorContext& ctx);

    const char* name() const override { return "Asset Browser"; }
    const char* icon() const override { return ICON_FA_BOOK; }

    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    ImVec2 window_padding() const override { return ImVec2(0.0f, 0.0f); }
    void draw_contents(EditorContext& ctx) override;
};

}
