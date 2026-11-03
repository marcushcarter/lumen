#pragma once
#include <core/project/project_settings.h>
#include <editor/tabs/editor_tab.h>
#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <string>

namespace lumen {

struct ProjectSettingsTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::PROJECT_SETTINGS;
    static constexpr const char* ICON = ICON_FA_WRENCH;
    static constexpr uint32_t MAX_TEXT = 256;

    ProjectSettings edit;
    ProjectSettings saved;
    SettingsLayout layout;
    std::string _title;
    char title_buf[MAX_TEXT] = {};
    char export_folder_buf[MAX_TEXT] = {};
    char export_exe_buf[MAX_TEXT] = {};

    const char* title() const override { return _title.c_str(); }
    ImU32 icon_color() const override { return ui_style().color.text_dim; }
    bool dirty() const override { return !(edit == saved); }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    bool _text_setting(const char* p_label, const char* p_hint, const char* p_tooltip, char* r_buf, std::string& r_value);
    void _draw_window(EditorContext& ctx);
    void _draw_export(EditorContext& ctx);
};

}
