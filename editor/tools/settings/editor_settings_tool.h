#pragma once
#include <editor/documents/editor_document.h>
#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <string>

namespace lumen {

struct EditorSettingsTool : EditorDocument
{
    static constexpr DocumentKind KIND = DocumentKind::EDITOR_SETTINGS;
    static constexpr const char* ICON = ICON_FA_GEAR;

    SettingsLayout layout;
    std::string _title;

    const char* title() const override { return _title.c_str(); }
    Error create(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _draw_appearance(EditorContext& ctx);
    void _draw_display(EditorContext& ctx);
    void _draw_startup(EditorContext& ctx);
};

}
