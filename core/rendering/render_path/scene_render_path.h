#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/geometry/geometry_feature.h>
#include <core/rendering/features/lighting/lighting_feature.h>
#include <core/rendering/features/post/post_process_feature.h>

namespace lumen {

struct SceneRenderPath : RenderPath
{
    GeometryFeature geometry;
    LightingFeature lighting;
    PostProcessFeature post;

    void _register_features() override {
        features.push_back(&geometry);
        features.push_back(&lighting);
        features.push_back(&post);
    }
};

}
