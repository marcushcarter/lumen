#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct ImGuiFeature : Feature
{
    RenderGraph::Pass ui_pass;
    const char* viewport = nullptr;

    Error create_resources() override;
    void build(RenderGraph& g) override;
};

}