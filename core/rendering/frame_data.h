#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/world_gpu.h>
#include <core/assets/guid.h>
#include <core/world/world_settings.h>
#include <core/base/error.h>
#include <vector>
#include <glm/glm.hpp>

namespace lumen {

struct StaticInstances
{
    std::vector<Instance> instances;
    std::vector<Transform> transforms;
    uint32_t cluster_ref_capacity = 0;

    uint64_t version = 0;
    uint64_t world_version = UINT64_MAX;
    uint64_t geometry_version = UINT64_MAX;
    uint64_t model_version = UINT64_MAX;
    std::vector<uint64_t> uploaded_version;

    uint32_t count() { return (uint32_t)instances.size(); }

    void clear() {
        instances.clear();
        transforms.clear();
        cluster_ref_capacity = 0;
        world_version = UINT64_MAX;
        geometry_version = UINT64_MAX;
        model_version = UINT64_MAX;
        version++;
    }
};

struct StaticLights
{
    std::vector<GpuLight> lights;

    uint64_t version = 0;
    uint64_t world_version = UINT64_MAX;
    std::vector<uint64_t> uploaded_version;

    uint32_t count() { return (uint32_t)lights.size(); }

    void clear() {
        lights.clear();
        world_version = UINT64_MAX;
        version++;
    }
};

struct FrameData
{
    std::vector<Instance> instances_scratch;
    std::vector<Transform> transforms_scratch;
    uint32_t instance_count = 0;
    uint32_t cluster_ref_capacity = 0;

    GpuLight directional[MAX_DIRECTIONAL_LIGHTS] = {};
    std::vector<GpuLight> lights_scratch;
    uint32_t directional_count = 0;
    uint32_t light_count = MAX_DIRECTIONAL_LIGHTS;

    float px_per_unit = 1.0f;
    bool hiz_history_valid = false;

    uint32_t output_width = 0;
    uint32_t output_height = 0;
    float texture_grad_scale = 1.0f;
    bool taa_reset = true;

    CameraUniform camera{};

    GpuAtmosphere atmosphere{};
    vec4 atmosphere_lut_key[2] = { vec4(-1.0f), vec4(-1.0f) };
    uint64_t atmosphere_lut_version = 0;
    LensFlareSettings flare;

    struct EntityCache {
        mat4 prev_mtx;
        uint64_t frame = 0;
        uint32_t generation = UINT32_MAX;
        uint32_t mesh_index = UINT32_MAX;
        uint32_t slot_base = 0;
        Guid model;
        uint64_t model_version = UINT64_MAX;
    };
    std::vector<EntityCache> entity_cache;

    StaticInstances static_insts;
    StaticLights static_lights;

    void reset();
};

}
