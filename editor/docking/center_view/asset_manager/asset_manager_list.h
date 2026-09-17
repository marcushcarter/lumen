#pragma once
#include <editor/editor_context.h>
#include <core/rendering/render_graph_profiler.h>
#include <imgui.h>
#include <filesystem>
#include <unordered_map>
#include <core/assets/guid.h>

namespace lumen {

struct AssetBrowserList
{
    // float card_width = 150.0f;
    // float card_height = 220.0f;
    
    float card_width = 75.0f;
    float card_height = 110.0f;

    std::filesystem::path rename_target;
    char rename_buf[256] = {};
    std::filesystem::path rename_delete_request;
    std::filesystem::path cancel_request;

    void _delete_content(EditorContext& ctx, const std::filesystem::path& p_asset);
    void _delete_asset(EditorContext& ctx, const std::filesystem::path& p_path);
    void _delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder);

    std::unordered_map<std::filesystem::path, Guid> _thumb_guids;
    Guid _resolve_texture_guid(const std::filesystem::path& p_path);

    void _list_item(ImTextureID p_icon, const char* p_main_text, const char* p_sub_text);
    bool _draw_card(ImTextureID p_texture, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress = 0.5, bool p_importing = false);
    void draw(EditorContext& ctx, std::filesystem::path& selected, const char* search_buf);
};

}