#pragma once
#include <core/rendering/features/feature.h>
#include <core/scene/scene.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace lumen {

struct OutlineFeature : Feature
{
    static constexpr uint32_t MIN_MASK_BYTES = 4096;
    static constexpr uint32_t MASK_MAX_INSTANCES = 256;
    static constexpr uint32_t MASK_MAX_CLUSTERS = 8u * 1024;
    static constexpr uint32_t MASK_MAX_TRIANGLES = 256u * 1024;
    static constexpr uint32_t MASK_MAX_INDICES = MASK_MAX_TRIANGLES * 3u;
    static constexpr uint32_t MASK_LIST_HEADER = 32;
    static constexpr uint32_t MASK_DRAW_STRIDE = 20;
    static constexpr uint32_t MASK_INSTANCES_HEADER = 16;
    static constexpr uint32_t MASK_GROUP_SIZE = 64;
    static constexpr float MASK_SCALE = 0.5f;
    static constexpr float MASK_MIN_DISTANCE = 0.1f;

    struct MaskInstance {
        uint32_t instance;
        uint32_t mesh;
        uint32_t slot_base;
        float lod_density;
    };

    struct MaskSlot {
        drivers::DeviceDriverVulkan::Buffer instances;
        drivers::DeviceDriverVulkan::Buffer list;
        drivers::DeviceDriverVulkan::Buffer indices;
        drivers::DeviceDriverVulkan::Buffer materials;
    };

    std::vector<drivers::DeviceDriverVulkan::Buffer> mask_buffers;
    std::vector<uint64_t> slot_version;

    std::vector<uint32_t> words;
    uint32_t selected_count = 0;
    uint64_t version = 0;
    uint64_t source_version = UINT64_MAX;

    glm::vec4 color = glm::vec4(1.0f, 0.54f, 0.16f, 1.0f);
    glm::vec4 hover_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    int radius = 2;
    float hidden_alpha = 0.35f;

    std::vector<MaskSlot> mask_slots;
    drivers::DeviceDriverVulkan::Sampler mask_sampler;
    uint32_t mask_count = 0;
    uint32_t mask_max_nodes = 0;
    bool pass_mask = false;

    bool hover = false;
    glm::vec2 hover_uv = glm::vec2(0.0f);
    glm::vec2 pass_hover_uv = glm::vec2(0.0f);

    glm::vec2 rect_min = glm::vec2(0.0f);
    glm::vec2 rect_max = glm::vec2(1.0f);
    glm::vec2 pass_rect_min = glm::vec2(0.0f);
    glm::vec2 pass_rect_max = glm::vec2(1.0f);

    RenderGraph::Pass outline_pass;
    RenderGraph::Pass hover_pass;
    RenderGraph::Pass mask_reset_pass;
    RenderGraph::Pass mask_select_pass;
    RenderGraph::Pass mask_draw_pass;
    drivers::DeviceDriverVulkan::Pipeline outline_pipe;
    drivers::DeviceDriverVulkan::Pipeline mask_select_pipe;
    drivers::DeviceDriverVulkan::Pipeline mask_draw_pipe;

    void _create_outline_pass();
    void _dispatch(RenderGraph::CommandList& cl, bool p_hover);
    void _create_mask_passes();
    bool _selected(uint32_t p_entity) const;
    void _gather_mask(MaskSlot& r_slot);

    void set_selection(const Entity* p_entities, uint32_t p_count);
    void set_rect(glm::vec2 p_min, glm::vec2 p_max) { rect_min = p_min; rect_max = p_max; }
    void set_hover(bool p_hover, glm::vec2 p_uv) { hover = p_hover; hover_uv = p_uv; }

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
