#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct PostProcessFeature;

struct TAAFeature : Feature
{
    // static constexpr const char* OUTPUT = "TAAOutput";
    // static constexpr const char* HISTORY = "TAAHistory";

    struct Retired {
        drivers::DeviceDriverVulkan::Image image;
        uint64_t serial = 0;
    };

    const PostProcessFeature* post = nullptr;

    RenderGraph::Pass resolve_pass;
    RenderGraph::Pass sharpen_pass;
    drivers::DeviceDriverVulkan::Pipeline resolve_pipe;
    drivers::DeviceDriverVulkan::Pipeline sharpen_pipe;
    drivers::DeviceDriverVulkan::Sampler linear_sampler;

    drivers::DeviceDriverVulkan::Image history[2];
    uint32_t history_index = 0;
    bool history_valid = false;
    bool suppressed = false;
    float sharpness = 1.0f;
    bool _reset = true;
    uint64_t _serial = 0;
    std::vector<Retired> _retired;

    void _create_resolve_pass();
    void _create_sharpen_pass();
    float _base_sharpness(float p_ratio) const;
    void _ensure_history(uint32_t p_width, uint32_t p_height);
    void _collect(bool p_all);

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}