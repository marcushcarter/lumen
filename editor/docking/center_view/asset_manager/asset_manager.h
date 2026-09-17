#pragma once
#include <editor/docking/center_view/debug_tab.h>
#include <editor/docking/center_view/asset_manager/asset_manager_list.h>
#include <editor/docking/center_view/asset_manager/asset_manager_toolbar.h>

#include <editor/editor_context.h>
#include <core/base/error.h>
#include <imgui.h>
#include <filesystem>

namespace lumen {
    
struct AssetManagerDebugTab : DebugTab
{
    std::filesystem::path selected_folder;
    char search_buf[256] = {};
    float split_x = 0.18f;

    AssetBrowserList list;
    AssetBrowserToolbar toolbar;
    
    void _draw_folder_node(const std::filesystem::path& dir, std::filesystem::path& selected, int depth);
    
    const char* name() const override { return "Asset Browser"; }
    void draw(EditorContext& ctx) override;
};

}