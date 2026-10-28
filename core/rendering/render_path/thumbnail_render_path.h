#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/editor/thumbnail_capture_feature.h>

namespace lumen {

struct ThumbnailRenderPath : SceneRenderPath
{
    ThumbnailCaptureFeature capture;

    void _register_features() override {
        SceneRenderPath::_register_features();
        features.push_back(&capture);
    }
};

}
