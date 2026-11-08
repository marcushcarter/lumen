#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace lumen {

struct CameraUniform;

struct GridFeature : Feature
{
    static constexpr float FADE_MIN = 60.0f;
    static constexpr float FADE_HEIGHT = 25.0f;

    struct Params {
        glm::vec4 clip_z;
        glm::vec4 clip_w;
        glm::vec4 camera;
        glm::vec4 ray;
        glm::vec4 ray_dx;
        glm::vec4 ray_dy;
        glm::vec4 grid_color;
        glm::vec4 axis_x_color;
        glm::vec4 axis_z_color;
        glm::uvec4 size;
    };

    RenderGraph::Pass grid_pass;
    drivers::DeviceDriverVulkan::Pipeline grid_pipe;
    std::vector<drivers::DeviceDriverVulkan::Buffer> param_buffers;

    bool show_grid = true;
    bool show_axes = true;
    glm::vec4 grid_color = glm::vec4(0.0887f, 0.0887f, 0.0887f, 1.0f);
    glm::vec4 axis_x_color = glm::vec4(1, 0, 0, 1);
    glm::vec4 axis_z_color = glm::vec4(0, 0, 1, 1);

    void _write_params(const CameraUniform& p_camera, VkExtent2D p_size, VkExtent2D p_depth_size, Params& r_params) const;
    void _create_grid_pass();

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
