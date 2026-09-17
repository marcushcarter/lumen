#pragma once
#include <editor/editor_context.h>
#include <core/rendering/render_graph_profiler.h>
#include <filesystem>

namespace lumen {

struct AssetBrowserToolbar
{
    void _breadcrumb(const std::filesystem::path& root, std::filesystem::path& selected);

    void draw_header(EditorContext& ctx, const std::filesystem::path& root, std::filesystem::path& selected, char* search_buf, size_t search_cap);
};

}