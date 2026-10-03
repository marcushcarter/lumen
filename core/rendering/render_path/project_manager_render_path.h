#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/present/imgui_feature.h>
#include <core/rendering/features/present/screenshot_feature.h>

namespace lumen {

struct ProjectManagerRenderPath : RenderPath
{
    ImGuiFeature ui;
    ScreenshotFeature screenshot;  

    ProjectManagerRenderPath() {
        ui.viewport = nullptr;
        features.push_back(&ui);
        features.push_back(&screenshot);
    }
};

}