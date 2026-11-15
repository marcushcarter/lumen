#pragma once
#include <core/base/error.h>
#include <editor/application/editor_context.h>
#include <editor/assets/content_browser_panel.h>
#include <editor/documents/scene/scene_document.h>
#include <editor/layout/editor_layout.h>
#include <editor/tools/gpu_profiler_panel.h>
#include <editor/tools/memory_panel.h>
#include <editor/tools/tool_panel.h>
#include <memory>
#include <string>

namespace lumen {

struct EditorDocument;

struct Workspace
{
    static constexpr int LAYOUT_VERSION = 4;

    EditorLayout layout;
    SceneDocument scene;
    ContentBrowserPanel content;
    GpuProfilerPanel gpu_profiler;
    MemoryPanel memory;
    std::unique_ptr<ToolPanel> version_control;
    std::unique_ptr<ToolPanel> editor_settings;
    std::unique_ptr<ToolPanel> project_settings;
    EditorDocument* active_document = nullptr;

    void _release_tools(EditorContext& ctx);
    void _sync_documents(EditorContext& ctx);
    void _sync_document_panels(EditorContext& ctx);
    void _draw_toolbar(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_tabs(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    void initialize();
    void shutdown(EditorContext& ctx);
    void reset(EditorContext& ctx);

    std::string document_name() const;
    bool dirty() const;
    Error save(EditorContext& ctx);

    void tick(EditorContext& ctx);
    void draw(EditorContext& ctx);
};

}
