#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/geometry/geometry_feature.h>
#include <core/rendering/features/lighting/light_cull_feature.h>
#include <core/rendering/features/lighting/lighting_feature.h>
#include <core/rendering/features/atmosphere/atmosphere_feature.h>
#include <core/rendering/features/post/auto_exposure_feature.h>
#include <core/rendering/features/post/taa_feature.h>
#include <core/rendering/features/post/bloom_feature.h>
#include <core/rendering/features/post/post_process_feature.h>

namespace lumen {

struct SceneRenderPath : RenderPath
{
    GeometryFeature geometry;
    LightCullFeature light_cull;
    AtmosphereFeature atmosphere;
    LightingFeature lighting;
    AutoExposureFeature exposure;
    TAAFeature taa;
    BloomFeature bloom;
    PostProcessFeature post;
    
    SceneRenderPath() {
        lighting.lut_sampler = &atmosphere.lut_sampler.bindless_sampler;
        taa.post = &post;
        post.taa = &taa;
        post.bloom = &bloom;
        features.push_back(&geometry);
        features.push_back(&light_cull);
        features.push_back(&atmosphere);
        features.push_back(&lighting);
        features.push_back(&exposure);
        features.push_back(&taa);
        features.push_back(&bloom);
        features.push_back(&post);
    }
};
    
}