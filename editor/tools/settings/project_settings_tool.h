#pragma once
#include <core/project/project_settings.h>
#include <editor/documents/editor_document.h>
#include <editor/layout/settings_layout.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <string>

namespace lumen {

struct ProjectSettingsTool : EditorDocument
{
    static constexpr DocumentKind KIND = DocumentKind::PROJECT_SETTINGS;
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
    bool dirty() const override { return !(edit == saved); }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    bool _text_setting(const char* p_label, const char* p_hint, const char* p_tooltip, char* r_buf, std::string& r_value);
    void _draw_game(EditorContext& ctx);
    void _draw_window(EditorContext& ctx);
    void _draw_export(EditorContext& ctx);
};

}
