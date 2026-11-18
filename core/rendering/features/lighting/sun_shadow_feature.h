#pragma once
#include <core/rendering/features/feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/scene_gpu.h>
#include <core/assets/guid.h>
#include <array>
#include <string>
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

    static constexpr uint32_t CASCADE_KINDS = 4;
    static constexpr uint32_t CASCADE_KIND_MIXED = 3;
    static constexpr uint32_t CASCADE_HEADER_WORDS = 50;
    static constexpr VkDeviceSize CASCADE_HEADER_SIZE = CASCADE_HEADER_WORDS * sizeof(uint32_t);
    static constexpr VkDeviceSize CASCADE_LIST_SIZE = CASCADE_HEADER_SIZE + (VkDeviceSize)SUN_CASCADE_MAX_CLUSTERS * sizeof(uvec2);
    static constexpr VkDeviceSize CASCADE_SPILL_SIZE = (VkDeviceSize)CASCADE_KINDS * SUN_CASCADE_MAX_CLUSTERS * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_DRAW_STRIDE = 5 * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_SPILL_DRAW_OFFSET = 20 * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_SPILL_DRAW_STRIDE = 4 * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_COMPACT_DISPATCH_OFFSET = 36 * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_CAPACITY_WORD = 40;
    static constexpr uint32_t CASCADE_FEEDBACK_OFFSET = 41 * sizeof(uint32_t);
    static constexpr uint32_t CASCADE_FEEDBACK_WORDS = 2 * CASCADE_KINDS;
    static constexpr uint32_t CASCADE_KIND_INITIAL[CASCADE_KINDS] = { 256u * 1024, 64u * 1024, 64u * 1024, 16u * 1024 };
    static constexpr uint32_t CASCADE_KIND_GRANULE = 16u * 1024;
    static constexpr uint32_t CASCADE_KIND_MAX = 8u * 1024 * 1024;
    static constexpr uint32_t CASCADE_SHRINK_FRAMES = 300;
    static constexpr float CASCADE_MASK_MIP_BIAS[SUN_CASCADE_COUNT] = { 0.0f, 1.0f };
    static constexpr uint32_t CASCADE_DISPATCH_MAX = 65535;
    static constexpr float CASCADE_LOD_DISTANCE[SUN_CASCADE_COUNT] = { 5.0f, 25.0f };
    static constexpr float MIN_CASTER_TEXELS = 2.0f;

    static constexpr uint32_t HEIGHTFIELD_LEVELS = 2;
    static constexpr uint32_t HEIGHTFIELD_LINE = 2048;
    static constexpr uint32_t HEIGHTFIELD_PREFIX = HEIGHTFIELD_LINE - SUN_HEIGHTFIELD_SIZE;
    static constexpr uint32_t HEIGHTFIELD_COLUMNS_PER_FRAME[HEIGHTFIELD_LEVELS] = { 256, 128 };
    static constexpr float HEIGHTFIELD_EXTENT[HEIGHTFIELD_LEVELS] = { 1000.0f, 8000.0f };
    static constexpr float HEIGHTFIELD_RECENTER[HEIGHTFIELD_LEVELS] = { 125.0f, 1000.0f };
    static constexpr uint32_t HEIGHTFIELD_SKIP[HEIGHTFIELD_LEVELS] = { 0, 8 };
    static constexpr float HEIGHTFIELD_FADE[HEIGHTFIELD_LEVELS][2] = { { 250.0f, 400.0f }, { 2600.0f, 3200.0f } };
    static_assert(HEIGHTFIELD_FADE[0][1] + 0.7072f * HEIGHTFIELD_RECENTER[0] < 0.5f * HEIGHTFIELD_EXTENT[0]);
    static_assert(HEIGHTFIELD_FADE[1][1] + 0.7072f * HEIGHTFIELD_RECENTER[1] < 0.5f * HEIGHTFIELD_EXTENT[1]);
    static constexpr float HEIGHTFIELD_BIAS_TEXELS = 0.25f;
    static constexpr float HEIGHTFIELD_NORMAL_OFFSET_TEXELS = 0.5f;
    static constexpr float HEIGHTFIELD_SUN_COS[HEIGHTFIELD_LEVELS] = { 0.99996192f, 0.99984770f };
    static constexpr uint64_t HEIGHTFIELD_BLEND_FRAMES = 30;
    static constexpr float HEIGHTFIELD_MIN_SUN_HEIGHT = 0.01f;
    static constexpr float HEIGHTFIELD_MIN_HORIZONTAL = 0.05f;
    static constexpr uint64_t HEIGHTFIELD_RESTREAM_FRAMES = 120;
    static_assert(HEIGHTFIELD_LINE > SUN_HEIGHTFIELD_SIZE);
    static_assert(SUN_HEIGHTFIELD_SIZE % HEIGHTFIELD_COLUMNS_PER_FRAME[0] == 0 && SUN_HEIGHTFIELD_SIZE % HEIGHTFIELD_COLUMNS_PER_FRAME[1] == 0);

    struct CascadeWork {
        drivers::DeviceDriverVulkan::Buffer buffer;
        VkDeviceSize capacity = 0;
    };

    struct HeightfieldMap {
        drivers::DeviceDriverVulkan::Image image;
        vec2 origin = vec2(0.0f);
        vec3 sun = vec3(0.0f);
        uint64_t terrain_epoch = 0;
        uint64_t terrain = 0;
        bool valid = false;
    };

    struct HeightfieldLevel {
        HeightfieldMap maps[2];
        uint32_t active = 0;
        bool building = false;
        uint32_t column = 0;
        uint64_t build_frame = 0;
        bool blending = false;
        uint64_t blend_start = 0;
        std::string name;
        std::string old_name;
    };

    struct HeightfieldInfo {
        uint32_t textures[2 * HEIGHTFIELD_LEVELS];
        vec4 area[2 * HEIGHTFIELD_LEVELS];
        vec4 axis[2 * HEIGHTFIELD_LEVELS];
        vec4 params;
        vec4 fade;
        vec4 blend;
    };

    std::vector<Slot> slots;
    std::vector<std::array<CascadeWork, SUN_CASCADE_COUNT>> cascade_work;
    uint32_t cascade_work_count[SUN_CASCADE_COUNT] = {};
    enum CascadeName { NAME_LIST, NAME_WORK, NAME_IMAGE, NAME_INDICES, NAME_MATERIALS, NAME_SPILL, NAME_FROZEN_LIST, NAME_FROZEN_INDICES, NAME_FROZEN_MATERIALS, NAME_FROZEN_SPILL, NAME_FEEDBACK, NAME_COUNT };

    struct CascadeFrozen {
        drivers::DeviceDriverVulkan::Buffer list;
        drivers::DeviceDriverVulkan::Buffer indices;
        drivers::DeviceDriverVulkan::Buffer materials;
        drivers::DeviceDriverVulkan::Buffer spill;
        VkDeviceSize index_size = 0;
        VkDeviceSize material_size = 0;
        bool ready = false;
    };

    struct CascadeFeedback {
        drivers::DeviceDriverVulkan::Buffer buffer;
        bool written = false;
    };

    uint32_t cascade_capacity[SUN_CASCADE_COUNT][CASCADE_KINDS] = {};
    uint32_t cascade_low_frames[SUN_CASCADE_COUNT][CASCADE_KINDS] = {};
    uint32_t cascade_demand[SUN_CASCADE_COUNT][CASCADE_KINDS] = {};
    uint32_t cascade_spilled[SUN_CASCADE_COUNT][CASCADE_KINDS] = {};
    std::vector<std::array<CascadeFeedback, SUN_CASCADE_COUNT>> cascade_feedback;

    std::string cascade_names[SUN_CASCADE_COUNT][NAME_COUNT];
    bool cascade_replay[SUN_CASCADE_COUNT] = {};
    drivers::DeviceDriverVulkan::Image cascade_images[SUN_CASCADE_COUNT];
    CascadeFrozen cascade_frozen[SUN_CASCADE_COUNT];
    uint32_t frozen_instance_count = 0;
    bool freeze = false;
    std::vector<uvec2> work_scratch;
    std::vector<uint32_t> cull_scratch;

    RenderGraph::Pass cascade_reset_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_select_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_dispatch_args_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_compact_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_draw_args_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_capture_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_feedback_pass[SUN_CASCADE_COUNT];
    RenderGraph::Pass cascade_draw_pass[SUN_CASCADE_COUNT];
    drivers::DeviceDriverVulkan::Pipeline cascade_select_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_args_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_compact_pipe;
    RenderGraph::Pass receiver_clear_pass;
    RenderGraph::Pass receiver_mask_pass;
    RenderGraph::Pass receiver_pyramid_pass;
    drivers::DeviceDriverVulkan::Pipeline receiver_mask_pipe;
    drivers::DeviceDriverVulkan::Pipeline receiver_pyramid_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_opaque_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_masked_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_two_sided_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_mixed_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_spill_opaque_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_spill_two_sided_pipe;
    drivers::DeviceDriverVulkan::Pipeline cascade_spill_masked_pipe;

    HeightfieldLevel heightfield[HEIGHTFIELD_LEVELS];
    std::vector<drivers::DeviceDriverVulkan::Buffer> heightfield_info;
    uint32_t heightfield_exec_level = 0;
    uint32_t heightfield_exec_slot = 0;
    uint32_t heightfield_exec_column = 0;
    uint32_t heightfield_exec_columns = 0;
    vec2 heightfield_exec_center = vec2(0.0f);
    vec3 heightfield_exec_sun = vec3(0.0f);
    std::string heightfield_target_name;
    uint64_t heightfield_frame = 0;
    uint64_t heightfield_epoch = 0;
    uint64_t heightfield_upload_frame = 0;
    RenderGraph::Pass heightfield_pass;
    drivers::DeviceDriverVulkan::Pipeline heightfield_pipe;

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
    void _create_cascade_passes(uint32_t p_cascade);
    void _cascade_casters(uint32_t p_cascade);
    bool _cascade_upload(uint32_t p_cascade);
    bool _cascade_frozen_alloc(uint32_t p_cascade);
    void _cascade_capacity_update(uint32_t p_cascade);
    void _cascade_bases(uint32_t p_cascade, uint32_t* r_base) const;
    VkDeviceSize _cascade_index_size(uint32_t p_cascade) const;
    VkDeviceSize _cascade_material_size(uint32_t p_cascade) const;
    void _cascade_frozen_free();
    void _create_receiver_passes();
    void _create_heightfield_pass();
    static vec2 _heightfield_axis(const vec3& p_dir);
    static float _heightfield_slope(const vec3& p_dir);
    static float _heightfield_texel(uint32_t p_level);
    bool _heightfield_dirty(uint32_t p_level, const vec2& p_center, const vec3& p_sun) const;
    void _heightfield_start(uint32_t p_level, const vec2& p_center, const vec3& p_sun);
    void _build_player(RenderGraph& g);
    void _build_cascades(RenderGraph& g);
    void _build_heightfield(RenderGraph& g);
    bool _slot_stale(const Slot& p_slot) const;
    void _slot_mark(Slot& r_slot) const;

    Error create_resources() override;
    Error create_pipelines() override;
    void destroy_resources() override;
    void build(RenderGraph& g) override;
};

}
