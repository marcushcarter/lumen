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

    enum class Tonemapper : uint32_t { AGX, ACES, PBR_NEUTRAL, NONE, AGX_PUNCHY, COUNT };

    static constexpr uint32_t TONEMAPPER_COUNT = (uint32_t)Tonemapper::COUNT;

    Tonemapper tonemapper = Tonemapper::AGX_PUNCHY;
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
