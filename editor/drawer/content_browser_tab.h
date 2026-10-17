#pragma once
#include <editor/drawer/drawer_tab.h>
#include <editor/assets/browser/asset_browser.h>

namespace lumen {

struct ContentBrowserDrawerTab : DrawerTab
{
    AssetBrowser browser;

    const char* name() const override { return "Content Browser"; }
    void draw(EditorContext& ctx) override { browser.draw(ctx); }
};

}
