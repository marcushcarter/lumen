#pragma once
#include <core/rendering/water_gpu.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/scene_gpu.h>
#include <core/rendering/debug_draw.h>
#include <core/rendering/instance_bvh.h>
#include <core/assets/guid.h>
#include <core/scene/scene_settings.h>
#include <core/base/error.h>
#include <vector>
#include <glm/glm.hpp>

namespace lumen {

struct StaticInstances
{
    std::vector<Instance> instances;
    std::vector<Transform> transforms;
    std::vector<uint32_t> cluster_counts;
    std::vector<vec3> bounds_min;
    std::vector<vec3> bounds_max;
    InstanceBvh bvh;
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
        cluster_counts.clear();
        bounds_min.clear();
        bounds_max.clear();
        bvh.clear();
        cluster_ref_capacity = 0;
        world_version = UINT64_MAX;
        geometry_version = UINT64_MAX;
        model_version = UINT64_MAX;
        version++;
    }
};

struct PoseComponent;

struct FrameData
{
    static constexpr uint32_t JOINT_FRAMES = 4;

    std::vector<Instance> instances_scratch;
    std::vector<Transform> transforms_scratch;
    std::vector<SkinInstance> skins_scratch;
    std::vector<uint32_t> candidates;
    uint32_t instance_count = 0;
    uint32_t candidate_count = 0;
    uint32_t cluster_ref_capacity = 0;

    DirectLight direct_light{ vec3(0.0f, -1.0f, 0.0f), 0.0f, vec3(0.0f), 0.0f };

    uint32_t player_instance = UINT32_MAX;
    uint32_t player_node_count = 0;
    uint32_t player_mesh = UINT32_MAX;
    uint32_t player_slot_base = 0;
    uint64_t skin_cache_header = 0;
    uint64_t player_skin_table = 0;
    float player_lod_density = 0.0f;
    vec3 player_box_min = vec3(0.0f);
    vec3 player_box_max = vec3(0.0f);

    float px_per_unit = 1.0f;
    float screen_px_per_unit = 1.0f;
    float aspect = 1.0f;

    vec4 sun_cascade_sphere[SUN_CASCADE_COUNT] = {};
    bool sun_cascade_reuse[SUN_CASCADE_COUNT] = {};
    bool sun_cascade_history[SUN_CASCADE_COUNT] = {};
    bool sun_heightfield = false;
    bool plain_materials = false;
    bool hiz_history_valid = false;

    uint32_t output_width = 0;
    uint32_t output_height = 0;
    float texture_grad_scale = 1.0f;

    CameraUniform camera{};

    std::vector<DebugLine> debug_lines;
    WaterGpuLink* water = nullptr;

    struct EntityCache {
        mat4 prev_mtx;
        uint64_t frame = 0;
        uint32_t generation = UINT32_MAX;
        uint32_t mesh_index = UINT32_MAX;
        uint32_t slot_base = 0;
        Guid model;
        uint64_t model_version = UINT64_MAX;
        std::vector<mat4> prev_skin;
        std::vector<mat4> prev_view_skin;
        vec3 prev_box_min = vec3(0.0f);
        vec3 prev_box_max = vec3(0.0f);
        uint64_t skin_revision = UINT64_MAX;
        uint32_t joint_base = UINT32_MAX;
        uint32_t joint_size = 0;
        uint64_t written_curr[JOINT_FRAMES] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
        uint64_t written_prev[JOINT_FRAMES] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
    };
    std::vector<EntityCache> entity_cache;

    struct SkinJob {
        uint32_t entity_index;
        uint32_t skin_id;
        const PoseComponent* pose;
        uint32_t mesh_index;
        bool continuous;
    };
    std::vector<SkinJob> skin_jobs;
    std::vector<uvec2> joint_free;
    uint32_t joint_top = 0;
    bool joint_reset = false;
    JointMatrices* joint_target = nullptr;
    uint32_t joint_frame = 0;

    StaticInstances static_insts;

    void reset();
    uint32_t joint_alloc(uint32_t p_count);
    void joint_release(EntityCache& r_cache);
    void joint_clear();
};

}
