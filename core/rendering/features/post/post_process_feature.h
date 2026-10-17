#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct PostProcessFeature : Feature
{
    enum Tonemapper : uint32_t { TONEMAPPER_AGX, TONEMAPPER_ACES, TONEMAPPER_PBR_NEUTRAL, TONEMAPPER_NONE, TONEMAPPER_COUNT };

    static constexpr const char* DISPLAY_COLOR = "DisplayColor";

    Tonemapper tonemapper = TONEMAPPER_AGX;
    float exposure_compensation_ev = 0.0f;
    bool dither = true;

    RenderGraph::Pass composite_pass;
    drivers::DeviceDriverVulkan::Pipeline composite_pipe;

    void _create_composite_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}