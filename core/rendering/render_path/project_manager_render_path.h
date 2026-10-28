#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/present/imgui_feature.h>

namespace lumen {

struct ProjectManagerRenderPath : RenderPath
{
    ImGuiFeature ui;

    ProjectManagerRenderPath() {
        ui.viewport = nullptr;
        features.push_back(&ui);
    }
};

}