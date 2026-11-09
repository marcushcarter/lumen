#pragma once
#include <editor/assets/asset_browser.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

struct AssetManager : AssetBrowser
{
    enum class NewAsset : uint8_t { WORLD, PREFAB, TERRAIN, MODEL, MATERIAL, BLEND_SPACE, OVERLAY, COUNT };

    float split_x = 0.18f;
    float split_files = 0.3f;

    bool delete_requested = false;
    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    bool rename_focus = false;
    std::filesystem::path cancel_request;

    void draw(EditorContext& ctx);
    void _draw_side(ImVec2 p_size);

    void _reset(const std::filesystem::path& p_root) override;
    void _draw_toolbar(EditorContext& ctx) override;
    float _item_margin() const override { return 0.0f; }
    float _item_gap() const override { return 0.0f; }
    ImVec2 _item_size(float p_avail_w) const override;
    bool _single_column() const override { return true; }
    std::string _item_subtitle(const Entry& p_entry) const override { return _relative(p_entry.path); }
    ItemResult _draw_item(EditorContext& ctx, const Item& p_item, ImVec2 p_size) override;
    void _tree_row_hook(const Dir& p_dir) override;
    void _item_hook(const Entry& p_entry) override;
    void _items_end(EditorContext& ctx) override;

    std::filesystem::path _target_folder(const EditorContext& ctx) const;
    bool _nested_in_selection(const std::filesystem::path& p_path) const;

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

    void _context_menu(EditorContext& ctx, const std::filesystem::path& p_row);
    void _create_asset(EditorContext& ctx, NewAsset p_kind);
    void _add_asset_menu(EditorContext& ctx);
    void _common_assets_menu(EditorContext& ctx);
};

}
