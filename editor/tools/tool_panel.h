#pragma once
#include <editor/layout/panel.h>
#include <editor/documents/editor_document.h>
#include <imgui.h>
#include <memory>

namespace lumen {

struct ToolPanel : Panel
{
    std::unique_ptr<EditorDocument> tool;
    const char* _name = nullptr;
    const char* _icon = nullptr;
    bool _created = false;

    ToolPanel(std::unique_ptr<EditorDocument> p_tool, const char* p_name, const char* p_icon) : tool(std::move(p_tool)), _name(p_name), _icon(p_icon) {}

    const char* name() const override { return _name; }
    const char* icon() const override { return _icon; }
    bool closable() const override { return true; }
    ImVec2 window_padding() const override { return ImVec2(0.0f, 0.0f); }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    void release(EditorContext& ctx) {
        if (_created) tool->destroy(ctx);
        _created = false;
    }

    void draw_contents(EditorContext& ctx) override {
        if (!_created) {
            if (tool->create(ctx) != Error::OK) return;
            _created = true;
        }
        tool->on_tick(ctx);
        tool->on_update(ctx, ImGui::GetIO().DeltaTime);
    }
};

}
