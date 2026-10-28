#pragma once
#include <core/rendering/render_context.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/features/feature.h>
#include <core/rendering/features/present/screenshot_feature.h>
#include <core/base/error.h>
#include <span>

namespace lumen {

struct RenderPath
{
    RenderContext ctx;

    std::vector<Feature*> features;
    uint32_t created_count = 0;
    ScreenshotFeature screenshot;

    Error create_resources();
    void destroy_resources();
    void build(RenderGraph& g);
    
    virtual ~RenderPath() = default;
};

}