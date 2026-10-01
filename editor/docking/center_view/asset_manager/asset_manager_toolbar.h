#pragma once
#include <editor/editor_context.h>
#include <editor/docking/center_view/asset_manager/asset_dir_cache.h>
#include <core/rendering/render_graph_profiler.h>
#include <filesystem>

namespace lumen {

struct AssetBrowserToolbar
{
    void _breadcrumb(const std::filesystem::path& root, std::filesystem::path& selected);

    void draw_header(EditorContext& ctx, AssetDirCache& cache, const std::filesystem::path& root, std::filesystem::path& selected, char* search_buf, size_t search_cap);
};

}