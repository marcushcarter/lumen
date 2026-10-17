#pragma once
#include <core/base/error.h>
#include <editor/editor_context.h>
#include <editor/tabs/editor_tab.h>
#include <editor/layout/editor_layout.h>

namespace lumen {

struct WorldTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::WORLD;
    static constexpr int VERSION = 1;

    EditorLayout layout;

    Error initialize();
    void shutdown();

    const char* title() const override { return "World"; }
    bool closable() const override { return false; }
    void on_update(EditorContext& ctx, float p_dt) override;

    void draw_menu();
    void take_screenshot(EditorContext& ctx);

    void _draw_toolbar(EditorContext& ctx);
    void _draw_play_controls(EditorContext& ctx, ImVec2 p_bar_min, ImVec2 p_bar_max);
};

}