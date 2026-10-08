#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <vector>

namespace lumen {

struct PickFeature : Feature
{
    static constexpr uint32_t NONE = 0xFFFFFFFF;

    RenderGraph::Pass pick_pass;
    drivers::DeviceDriverVulkan::Pipeline pick_pipe;
    std::vector<drivers::DeviceDriverVulkan::Buffer> readback;
    std::vector<uint32_t> slot_serial;

    bool pending = false;
    float pending_u = 0.0f;
    float pending_v = 0.0f;
    float pass_u = 0.0f;
    float pass_v = 0.0f;
    uint32_t request_serial = 0;

    bool result_ready = false;
    uint32_t result = NONE;

    void request(float p_u, float p_v);
    bool take_result(uint32_t& r_entity_index);

    void _create_pick_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}