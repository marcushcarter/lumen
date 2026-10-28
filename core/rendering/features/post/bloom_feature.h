#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <cstdint>

namespace lumen {

struct BloomFeature : Feature
{
    static constexpr uint32_t MAX_MIPS = 7;
    static constexpr uint32_t MIN_MIP_SIZE = 4;
    static constexpr const char* DOWN_NAMES[MAX_MIPS] = { "BloomDown0", "BloomDown1", "BloomDown2", "BloomDown3", "BloomDown4", "BloomDown5", "BloomDown6" };
    static constexpr const char* UP_NAMES[MAX_MIPS - 1] = { "Bloom", "BloomUp1", "BloomUp2", "BloomUp3", "BloomUp4", "BloomUp5" };

    float intensity = 0.04f;

    uint32_t mip_count = 0;
    uint32_t mip_width[MAX_MIPS] = {};
    uint32_t mip_height[MAX_MIPS] = {};
    const char* source = "SceneColor";

    RenderGraph::Pass down_passes[MAX_MIPS];
    RenderGraph::Pass up_passes[MAX_MIPS - 1];
    drivers::DeviceDriverVulkan::Pipeline down_pipe;
    drivers::DeviceDriverVulkan::Pipeline up_pipe;
    drivers::DeviceDriverVulkan::Sampler linear_sampler;

    void _create_down_pass(uint32_t p_mip);
    void _create_up_pass(uint32_t p_mip);
    const char* _up_source(uint32_t p_mip) const;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
