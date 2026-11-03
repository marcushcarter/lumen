#pragma once
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

struct EditorContext;

struct SettingsLayout
{
    struct Page {
        std::string label;
        std::function<void(EditorContext&)> draw;
    };

    std::vector<Page> pages;
    int selected = 0;

    void add(const char* p_label, std::function<void(EditorContext&)> p_draw) { pages.push_back({ p_label, std::move(p_draw) }); }
    void draw(EditorContext& ctx, const char* p_host_id, const std::function<void(EditorContext&)>& p_toolbar);
    void _draw_nav(ImVec2 p_size);
};

}
