#pragma once
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <imgui.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lumen {

struct AssetBrowser
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
        std::filesystem::path path;
        std::string path_str;
        std::string name;
        std::vector<std::filesystem::path> subdirs;
        std::vector<Entry> entries;
        bool exists = false;
    };

    struct TreeRow {
        const Dir* dir;
        uint16_t depth;
    };

    struct Item {
        ImTextureID texture = 0;
        const char* icon = nullptr;
        ImU32 icon_color = 0;
        const char* name = nullptr;
        const char* subtitle = nullptr;
        const std::filesystem::path* path = nullptr;
        float progress = -1.0f;
        bool importing = false;
    };

    struct ItemResult {
        bool clicked = false;
        bool activated = false;
    };

    std::unordered_map<std::filesystem::path, Dir> dirs;
    double last_refresh = -1.0e9;
    bool refresh_requested = false;
    AssetRegistry* registry = nullptr;

    std::filesystem::path root;
    std::filesystem::path selected_folder;
    std::vector<TreeRow> tree_rows;
    std::unordered_set<std::filesystem::path> open_folders;

    char search_buf[256] = {};
    std::vector<std::filesystem::path> selection;
    std::filesystem::path anchor;
    std::filesystem::path _selection_folder;
    std::filesystem::path _scroll_target;
    AssetDragPayload drag_payload;
    std::unordered_map<std::filesystem::path, AssetInfo> _thumb_infos;
    std::vector<uint32_t> _visible;

    virtual ~AssetBrowser() = default;

    static const char* icon_for(const std::filesystem::path& p_path, bool p_is_dir);
    static std::string type_name(std::string p_ext);

    void request_refresh() { refresh_requested = true; if (registry) registry->request_rebuild(); }
    void prepare(EditorContext& ctx);
    virtual void _reset(const std::filesystem::path& p_root);

    void _cache_tick(double p_now);
    Dir& _cache_get(const std::filesystem::path& p_dir);
    void _cache_scan(const std::filesystem::path& p_dir, Dir& r_dir);

    void _tree_emit(const std::filesystem::path& p_dir, uint16_t p_depth);
    void _tree_toggle(const std::filesystem::path& p_dir);
    void draw_tree(ImVec2 p_origin, float p_width, float p_height, bool p_vertical);
    void _draw_tree_row(const TreeRow& p_row, float p_row_h);

    bool _is_selected(const std::filesystem::path& p_path) const;
    void _select_only(const std::filesystem::path& p_path);
    void _clear_selection();
    void _click(const Dir& p_dir, uint32_t p_visible, bool p_ctrl, bool p_shift);

    AssetInfo _resolve_asset(const std::filesystem::path& p_path);
    std::string _relative(const std::filesystem::path& p_path) const;
    bool open_entry(EditorContext& ctx, const std::filesystem::path& p_path);

    void _drag_source(const Item& p_item);
    void _draw_thumb(ImDrawList* p_dl, const Item& p_item, ImVec2 p_min, ImVec2 p_max, float p_icon_size) const;
    void _draw_progress(ImDrawList* p_dl, const Item& p_item, ImVec2 p_min, ImVec2 p_max) const;

    void draw_items_column(EditorContext& ctx, ImVec2 p_size);
    void draw_items(EditorContext& ctx);
    void draw_footer();

    virtual void _draw_toolbar(EditorContext& ctx) = 0;
    virtual float _item_margin() const = 0;
    virtual float _item_gap() const = 0;
    virtual ImVec2 _item_size(float p_avail_w) const = 0;
    virtual bool _single_column() const = 0;
    virtual std::string _item_subtitle(const Entry& p_entry) const = 0;
    virtual ItemResult _draw_item(EditorContext& ctx, const Item& p_item, ImVec2 p_size) = 0;
    virtual void _tree_row_hook(const Dir& p_dir) { (void)p_dir; }
    virtual void _item_hook(const Entry& p_entry) { (void)p_entry; }
    virtual void _items_end(EditorContext& ctx) { (void)ctx; }
};

}
