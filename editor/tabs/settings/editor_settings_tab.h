#pragma once
#include <editor/tabs/editor_tab.h>
#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <string>

namespace lumen {

struct EditorSettingsTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::EDITOR_SETTINGS;
    static constexpr const char* ICON = ICON_FA_GEAR;

    SettingsLayout layout;
    std::string _title;

    const char* title() const override { return _title.c_str(); }
    ImU32 icon_color() const override { return ui_style().color.text_dim; }
    Error create(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _apply_theme(EditorContext& ctx);
    void _draw_appearance(EditorContext& ctx);
    void _draw_display(EditorContext& ctx);
    void _draw_startup(EditorContext& ctx);
};

}
