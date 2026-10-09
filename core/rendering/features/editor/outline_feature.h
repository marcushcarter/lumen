#pragma once
#include <core/rendering/features/feature.h>
#include <core/world/world.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace lumen {

struct OutlineFeature : Feature
{
    static constexpr uint32_t MIN_MASK_BYTES = 4096;

    RenderGraph::Pass mask_pass;
    RenderGraph::Pass outline_pass;
    drivers::DeviceDriverVulkan::Pipeline mask_pipe;
    drivers::DeviceDriverVulkan::Pipeline outline_pipe;

    std::vector<drivers::DeviceDriverVulkan::Buffer> mask_buffers;
    std::vector<uint64_t> slot_version;

    std::vector<uint32_t> words;
    uint32_t selected_count = 0;
    uint64_t version = 0;
    uint64_t source_version = UINT64_MAX;
    uint32_t pass_word_count = 0;

    glm::vec4 color = glm::vec4(1.0f, 0.262f, 0.0f, 1.0f);
    int radius = 2;

    glm::vec2 rect_min = glm::vec2(0.0f);
    glm::vec2 rect_max = glm::vec2(1.0f);

    void set_selection(const Entity* p_entities, uint32_t p_count);
    void set_rect(glm::vec2 p_min, glm::vec2 p_max) { rect_min = p_min; rect_max = p_max; }
    bool _pixel_rect(VkExtent2D p_extent, uint32_t (&r_rect)[4]) const;

    void _create_mask_pass();
    void _create_outline_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}