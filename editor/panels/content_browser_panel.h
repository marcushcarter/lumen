#pragma once
#include <editor/assets/asset_browser.h>
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct ContentBrowserPanel : Panel, AssetBrowser
{
    float split_x = 0.18f;
    float split_y = 0.35f;
    bool vertical = false;

    const char* name() const override { return ICON_FA_FOLDER "  Content Browser"; }
    bool in_footer() const override { return true; }
    bool closable() const override { return true; }
    bool default_open() const override { return false; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;

    void _draw_toolbar(EditorContext& ctx) override;
    float _item_margin() const override { return ui_style().outliner.pad_x; }
    float _item_gap() const override { return ui_style().asset_browser.card_gap; }
    ImVec2 _item_size(float p_avail_w) const override;
    bool _single_column() const override { return false; }
    std::string _item_subtitle(const Entry& p_entry) const override { return p_entry.type; }
    ItemResult _draw_item(EditorContext& ctx, const Item& p_item, ImVec2 p_size) override;
};

}
