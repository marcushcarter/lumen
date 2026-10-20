#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>

namespace lumen {

struct TAAFeature;
struct BloomFeature;

struct PostProcessFeature : Feature
{
    static constexpr float NIGHT_START_SUN_Y = -0.0349f;
    static constexpr float NIGHT_FULL_SUN_Y = -0.2079f;

    enum Tonemapper : uint32_t { TONEMAPPER_AGX, TONEMAPPER_ACES, TONEMAPPER_PBR_NEUTRAL, TONEMAPPER_NONE, TONEMAPPER_AGX_PUNCHY, TONEMAPPER_COUNT } tonemapper = TONEMAPPER_AGX_PUNCHY;
    float exposure_compensation_ev = 0.0f;
    float night_shift = 0.6f;

    const char* source = "SceneColor";
    TAAFeature* taa = nullptr;
    const BloomFeature* bloom = nullptr;

    RenderGraph::Pass composite_pass;
    drivers::DeviceDriverVulkan::Pipeline composite_pipe;

    void _create_composite_pass();
    float night_factor() const;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}