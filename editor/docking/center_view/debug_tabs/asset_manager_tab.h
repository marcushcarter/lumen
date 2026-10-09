#pragma once
#include <editor/docking/center_view/debug_tab.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/editor_context.h>
#include <core/assets/guid.h>
#include <imgui.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct AssetManagerDebugTab : DebugTab
{
    /***************/
    /**** CACHE ****/
    /***************/

    static constexpr double CACHE_REFRESH_INTERVAL_S = 1.0;

    struct Entry
    {
        std::filesystem::path path;
        std::string name;
        std::string type;
        std::string lower_name;
        Guid guid;
        bool is_dir = false;
        bool is_texture = false;
        bool guid_checked = false;
    };

    struct Dir
    {
        std::string path_str;
        std::string name;
        std::vector<std::filesystem::path> subdirs;
        std::vector<Entry> entries;
        bool exists = false;
    };

    std::unordered_map<std::filesystem::path, Dir> dirs;
    double last_refresh = -1.0e9;
    bool refresh_requested = false;

    void request_refresh() { refresh_requested = true; }
    void _cache_tick(double p_now);
    Dir& _cache_get(const std::filesystem::path& p_dir);
    void _cache_scan(const std::filesystem::path& p_dir, Dir& r_dir);

    /**************/
    /**** TREE ****/
    /**************/

    std::filesystem::path selected_folder;
    float split_x = 0.18f;

    void _draw_folder_node(const std::filesystem::path& dir, int depth);

    /*****************/
    /**** TOOLBAR ****/
    /*****************/

    char search_buf[256] = {};

    void _toolbar_breadcrumb(const std::filesystem::path& root);
    void _toolbar_draw(EditorContext& ctx, const std::filesystem::path& root);

    /**************/
    /**** LIST ****/
    /**************/

    float card_width = 75.0f;
    float card_height = 110.0f;

    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    std::filesystem::path rename_delete_request;
    std::filesystem::path cancel_request;

    AssetDragPayload drag_payload;

    std::unordered_map<std::filesystem::path, Guid> _thumb_guids;
    std::vector<uint32_t> _visible;

    void _delete_content(EditorContext& ctx, const std::filesystem::path& p_asset);
    void _delete_asset(EditorContext& ctx, const std::filesystem::path& p_path);
    void _delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder);
    Guid _resolve_texture_guid(const std::filesystem::path& p_path);
    bool _list_item(ImTextureID p_texture, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress = 0.5, bool p_importing = false);
    void _list_draw(EditorContext& ctx);

    /*******************/
    /**** DEBUG TAB ****/
    /*******************/

    const char* name() const override { return "Content Browser"; }
    void draw(EditorContext& ctx) override;
};

}
