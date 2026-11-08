#pragma once
#include <core/base/error.h>
#include <editor/application/editor_context.h>
#include <editor/layout/editor_layout.h>
#include <editor/tabs/asset_manager/asset_manager.h>
#include <editor/tabs/editor_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct AssetManagerTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::ASSET_MANAGER;
    static constexpr const char* ICON = ICON_FA_FOLDER;

    EditorLayout layout;
    AssetManager manager;

    Error initialize();
    void shutdown();
    void _draw_browser(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    const char* title() const override { return ICON_FA_FOLDER "  Asset Manager"; }
    ImU32 icon_color() const override { return ui_style().color.text_dim; }
    bool closable() const override { return false; }
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
