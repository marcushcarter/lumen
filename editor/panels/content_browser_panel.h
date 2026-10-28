#pragma once
#include <editor/layout/panel.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/editor_context.h>
#include <core/assets/guid.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct ContentBrowserPanel : Panel
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
        const char* icon = nullptr;
        bool is_dir = false;
        bool is_texture = false;
        bool guid_checked = false;
        AssetType asset_type = AssetType::NONE;
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
    AssetRegistry* registry = nullptr;
    EditorContext* _ctx = nullptr;

    void request_refresh() { refresh_requested = true; if (registry) registry->request_rebuild(); }
    void _cache_tick(double p_now);
    Dir& _cache_get(const std::filesystem::path& p_dir);
    void _cache_scan(const std::filesystem::path& p_dir, Dir& r_dir);

    /**************/
    /**** TREE ****/
    /**************/

    std::filesystem::path selected_folder;
    std::filesystem::path _root;
    float split_x = 0.18f;

    void _draw_folder_node(const std::filesystem::path& dir, int depth);
    void _reset_root(const std::filesystem::path& p_root);

    /**************/
    /**** LIST ****/
    /**************/
    
    char search_buf[256] = {};

    struct ItemResult {
        bool clicked = false;
        bool activated = false;
        bool context = false;
    };

    std::vector<std::filesystem::path> selection;
    std::filesystem::path anchor;
    std::filesystem::path _selection_folder;
    bool delete_requested = false;
    bool delete_confirm_open = false;
    char delete_message[160] = {};
    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    bool rename_focus = false;
    bool _scroll_to_rename = false;
    std::filesystem::path cancel_request;

    AssetDragPayload drag_payload;

    std::unordered_map<std::filesystem::path, AssetInfo> _thumb_infos;
    std::vector<uint32_t> _visible;

    struct ThumbView { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; };
    struct ThumbRetired { VkImageView view = VK_NULL_HANDLE; uint64_t frame = 0; };
    std::unordered_map<Guid, ThumbView> _thumb_views;
    std::vector<ThumbRetired> _thumb_retired;
    VkDevice _thumb_device = VK_NULL_HANDLE;

    void _delete_content(EditorContext& ctx, const std::filesystem::path& p_asset);
    void _delete_asset(EditorContext& ctx, const std::filesystem::path& p_path);
    void _delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder);
    AssetInfo _resolve_asset(const std::filesystem::path& p_path);
    float _row_height() const;
    void _new_folder(EditorContext& ctx);
    void _new_model(EditorContext& ctx);
    void _duplicate_selection(EditorContext& ctx);
    void _new_subfolder(EditorContext& ctx, const std::filesystem::path& p_parent);
    void _import(EditorContext& ctx);
    void _move_selection(const std::filesystem::path& p_dst);
    void _delete_folders_only(EditorContext& ctx);
    void _drop_target(const std::filesystem::path& p_dst);
    void _add_asset_menu(EditorContext& ctx);
    void _context_menu(EditorContext& ctx, const std::filesystem::path* p_row);
    void _draw_footer(uint32_t p_count);
    void _rename_begin(const std::filesystem::path& p_path);
    void _rename_commit();
    bool _is_selected(const std::filesystem::path& p_path) const;
    void _select_only(const std::filesystem::path& p_path);
    void _clear_selection();
    void _click(const Dir& p_dir, uint32_t p_visible, bool p_ctrl, bool p_shift);
    void _delete_selection();
    void _delete_now(EditorContext& ctx);
    void _draw_delete_confirm(EditorContext& ctx);
    ItemResult _list_item(ImTextureID p_texture, const char* p_icon, ImU32 p_icon_col, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress = -1.0f, bool p_importing = false);
    void _list_draw(EditorContext& ctx);
    VkImageView thumbnail(EditorContext& ctx, Guid p_guid, AssetType p_type);
    VkImageView _thumb_view(EditorContext& ctx, Guid p_guid, const drivers::DeviceDriverVulkan::Image& p_image);
    void _thumb_collect(EditorContext& ctx);

    /**************/
    /**** DRAW ****/
    /**************/

    const char* name() const override { return "Content Browser"; }
    bool in_footer() const override { return true; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;
    void shutdown() override;
};

}
