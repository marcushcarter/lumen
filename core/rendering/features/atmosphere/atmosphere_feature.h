#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <glm/glm.hpp>

namespace lumen {

struct AtmosphereFeature : Feature
{
    static constexpr uint32_t TRANSMITTANCE_WIDTH = 256;
    static constexpr uint32_t TRANSMITTANCE_HEIGHT = 64;
    static constexpr uint32_t MULTISCATTER_SIZE = 32;
    static constexpr uint32_t SKYVIEW_WIDTH = 256;
    static constexpr uint32_t SKYVIEW_HEIGHT = 144;
    static constexpr uint32_t AERIAL_SIZE = 32;
    static constexpr uint32_t AERIAL_SLICES = 32;
    static constexpr uint32_t SKY_SH_SIZE = 9 * 4 * sizeof(float);

    static constexpr float SKYVIEW_REBUILD_COS = 0.99999962f;
    static constexpr float SKYVIEW_REBUILD_ALTITUDE_KM = 0.001f;

    struct SkyViewKey {
        glm::vec3 sun_direction = glm::vec3(0.0f);
        glm::vec3 moon_direction = glm::vec3(0.0f);
        float altitude_km = -1.0f;
        float mie_g = 0.0f;
        uint64_t lut_version = UINT64_MAX;
    };

    drivers::DeviceDriverVulkan::Image transmittance_lut;
    drivers::DeviceDriverVulkan::Image multiscatter_lut;
    // drivers::DeviceDriverVulkan::Image skyview_sun_lut;
    // drivers::DeviceDriverVulkan::Image skyview_moon_lut;
    drivers::DeviceDriverVulkan::Image skyview_lut;
    drivers::DeviceDriverVulkan::Sampler lut_sampler;
    uint64_t built_lut_version = UINT64_MAX;
    SkyViewKey skyview_built;
    SkyViewKey skyview_pending;
    bool skyview_valid = false;

    RenderGraph::Pass transmittance_pass;
    RenderGraph::Pass multiscatter_pass;
    RenderGraph::Pass skyview_pass;
    RenderGraph::Pass aerial_pass;
    RenderGraph::Pass sky_lighting_pass;
    drivers::DeviceDriverVulkan::Pipeline transmittance_pipe;
    drivers::DeviceDriverVulkan::Pipeline multiscatter_pipe;
    drivers::DeviceDriverVulkan::Pipeline skyview_pipe;
    drivers::DeviceDriverVulkan::Pipeline aerial_pipe;
    drivers::DeviceDriverVulkan::Pipeline sky_lighting_pipe;

    drivers::DeviceDriverVulkan::Pipeline _compute_pipeline(const wchar_t* p_resource, const char* p_name);
    drivers::DeviceDriverVulkan::Image _create_lut(uint32_t p_width, uint32_t p_height, const char* p_name);

    void _create_transmittance_pass();
    void _create_multiscatter_pass();
    void _create_skyview_pass();
    void _create_aerial_pass();
    void _create_sky_lighting_pass();
    SkyViewKey _skyview_key() const;
    bool _skyview_dirty(const SkyViewKey& p_key) const;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}