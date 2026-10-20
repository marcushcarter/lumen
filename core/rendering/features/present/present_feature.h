#pragma once
#include <core/rendering/features/feature.h>

namespace lumen {

struct PresentFeature : Feature
{    
    RenderGraph::Pass present_pass;
    drivers::DeviceDriverVulkan::Pipeline blit_pipe;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}