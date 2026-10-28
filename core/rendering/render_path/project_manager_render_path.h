#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/present/imgui_feature.h>

namespace lumen {

struct ProjectManagerRenderPath : RenderPath
{
    ImGuiFeature ui;

    void _register_features() override {
        ui.viewport = nullptr;
        features.push_back(&ui);
    }
};

}
