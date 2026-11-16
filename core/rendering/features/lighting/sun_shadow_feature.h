#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/scene_gpu.h>
#include <vector>

namespace lumen {

struct SunShadowFeature : Feature
{
    static constexpr uint32_t GROUP_SIZE = 64;
    static constexpr uint32_t LIST_HEADER_WORDS = 20;
    static constexpr uint32_t LIST_SKIN_DISPATCH_OFFSET = 14 * sizeof(uint32_t);
    static constexpr VkDeviceSize LIST_HEADER_SIZE = LIST_HEADER_WORDS * sizeof(uint32_t);
    static constexpr uint32_t DRAW_STRIDE = 5 * sizeof(uint32_t);

    struct Slot {
        drivers::DeviceDriverVulkan::Buffer list;
        drivers::DeviceDriverVulkan::Buffer indices;
        drivers::DeviceDriverVulkan::Buffer materials;
        uint32_t mesh = UINT32_MAX;
        uint32_t slot_base = UINT32_MAX;
        float lod_density = 0.0f;
        uint64_t geometry_version = UINT64_MAX;
        uint64_t residency_version = UINT64_MAX;
    };

    std::vector<Slot> slots;

    RenderGraph::Pass reset_pass;
    RenderGraph::Pass select_pass;
    RenderGraph::Pass finalize_pass;
    RenderGraph::Pass draw_pass;
    RenderGraph::Pass skin_pass;
    drivers::DeviceDriverVulkan::Pipeline skin_pipe;
    drivers::DeviceDriverVulkan::Pipeline select_pipe;
    drivers::DeviceDriverVulkan::Pipeline finalize_pipe;
    drivers::DeviceDriverVulkan::Pipeline draw_pipe;
    drivers::DeviceDriverVulkan::Pipeline masked_pipe;
    drivers::DeviceDriverVulkan::Sampler material_sampler;

    void _create_reset_pass();
    void _create_select_pass();
    void _create_finalize_pass();
    void _create_draw_pass();
    void _create_skin_pass();
    bool _slot_stale(const Slot& p_slot) const;
    void _slot_mark(Slot& r_slot) const;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
