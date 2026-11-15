#pragma once
#include <editor/application/editor_context.h>
#include <editor/layout/panel.h>
#include <imgui.h>
#include <functional>
#include <memory>
#include <vector>

namespace lumen {

struct DocumentLayout
{
    static constexpr float MIN_SPLIT = 0.15f;
    static constexpr float MAX_SPLIT = 0.85f;

    std::vector<std::unique_ptr<Panel>> panels;
    std::function<void(EditorContext&, ImVec2, ImVec2)> center;
    float split_x = 0.62f;
    float split_y = 0.68f;
    ImVec2 area_min = ImVec2(0.0f, 0.0f);
    ImVec2 area_max = ImVec2(0.0f, 0.0f);

    template <class T, class... A>
    T* add_panel(A&&... a) {
        auto up = std::make_unique<T>(std::forward<A>(a)...);
        T* raw = up.get();
        panels.push_back(std::move(up));
        return raw;
    }

    void _draw_left(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_side(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar);
    void shutdown();
};

}
