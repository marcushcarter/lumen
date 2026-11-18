#include <editor/layout/document_layout.h>
#include <editor/ui/ui_widgets.h>
#include <imgui_internal.h>

namespace lumen {

void DocumentLayout::shutdown()
{
    for (auto& p : panels) p->shutdown();
    panels.clear();
}

void DocumentLayout::_draw_left(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    std::vector<Panel*> bottom;
    for (auto& p : panels) if (p->slot() == PanelSlot::BOTTOM) bottom.push_back(p.get());
    if (bottom.empty()) {
        if (center) center(ctx, p_min, p_max);
        return;
    }
    const float h = ImMax(1.0f, p_max.y - p_min.y);
    split_y = ImClamp(split_y, MIN_SPLIT, MAX_SPLIT);
    const float split = p_min.y + ImFloor(h * split_y);
    const SplitterState s = ui_edge_splitter("##window_split_y", SplitAxis::Y, ImVec2(p_min.x, split), ImVec2(p_max.x, split));
    if (s.active) split_y = ImClamp(split_y + s.delta / h, MIN_SPLIT, MAX_SPLIT);
    const float top_h = ImFloor(h * split_y);
    if (center) center(ctx, p_min, ImVec2(p_max.x, p_min.y + top_h));
    const float y0 = p_min.y + top_h;
    const float each = ImFloor((p_max.y - y0) / (float)bottom.size());
    for (size_t i = 0; i < bottom.size(); ++i) {
        const float a = y0 + each * (float)i;
        const float b = i + 1 == bottom.size() ? p_max.y : a + each;
        panel_draw(ctx, *bottom[i], ImVec2(p_min.x, a), ImVec2(p_max.x, b));
    }
}

void DocumentLayout::_draw_side(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    std::vector<Panel*> side;
    for (auto& p : panels) if (p->slot() == PanelSlot::SIDE) side.push_back(p.get());
    const float each = ImFloor((p_max.y - p_min.y) / (float)ImMax((size_t)1, side.size()));
    for (size_t i = 0; i < side.size(); ++i) {
        const float a = p_min.y + each * (float)i;
        const float b = i + 1 == side.size() ? p_max.y : a + each;
        panel_draw(ctx, *side[i], ImVec2(p_min.x, a), ImVec2(p_max.x, b));
    }
}

void DocumentLayout::draw(EditorContext& ctx, const std::function<void(EditorContext&)>& p_toolbar)
{
    const UiStyle& ui = ui_style();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 end(origin.x + ImMax(1.0f, avail.x), origin.y + ImMax(1.0f, avail.y));
    area_min = origin;
    area_max = end;

    float body_y = origin.y;
    if (p_toolbar) {
        const float bar_h = ui.toolbar.height;
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##window_toolbar", ImVec2(end.x - origin.x, bar_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        p_toolbar(ctx);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::EndChild();
        body_y += bar_h;
    }

    bool has_side = false;
    for (auto& p : panels) has_side |= p->slot() == PanelSlot::SIDE;
    const ImVec2 body_min(origin.x, body_y);
    if (!has_side) {
        _draw_left(ctx, body_min, end);
        return;
    }

    const float w = ImMax(1.0f, end.x - origin.x);
    split_x = ImClamp(split_x, MIN_SPLIT, MAX_SPLIT);
    const float split = origin.x + ImFloor(w * split_x);
    const SplitterState s = ui_edge_splitter("##window_split_x", SplitAxis::X, ImVec2(split, body_y), ImVec2(split, end.y));
    if (s.active) split_x = ImClamp(split_x + s.delta / w, MIN_SPLIT, MAX_SPLIT);
    const float left_w = ImFloor(w * split_x);
    _draw_left(ctx, body_min, ImVec2(origin.x + left_w, end.y));
    _draw_side(ctx, ImVec2(origin.x + left_w, body_y), end);
}

}
