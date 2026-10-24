#pragma once
#include <core/rendering/render_view.h>
#include <core/rendering/render_path/thumbnail_render_path.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmaterial.h>
#include <core/assets/lmodel.h>
#include <core/assets/guid.h>
#include <core/world/world.h>
#include <core/world/camera.h>
#include <core/base/error.h>
#include <core/base/jobs.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <deque>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace lumen {

struct EditorContext;

struct ThumbnailCache
{
    static constexpr uint32_t VERSION = 1;
    static constexpr uint32_t RENDER_SIZE = 256;
    static constexpr uint32_t OUTPUT_SIZE = ThumbnailCaptureFeature::OUTPUT_SIZE;
    static constexpr double CHECK_INTERVAL_S = 1.0;
    static constexpr uint64_t VISIBLE_FRAMES = 2;
    static constexpr uint64_t RENDER_TIMEOUT_FRAMES = 16;
    static constexpr uint32_t START_ATTEMPTS = 4;
    static constexpr float CAMERA_FOV_DEG = 30.0f;
    static constexpr float CAMERA_YAW_DEG = 35.0f;
    static constexpr float CAMERA_PITCH_DEG = 20.0f;
    static constexpr float FRAME_MARGIN = 1.05f;
    static constexpr float TIME_OF_DAY = 12.0f;

    enum class JobState : uint8_t { NONE, PREPARED, RENDERING };
    enum class Readiness : uint8_t { READY, WAIT, FAIL };

    struct Entry {
        AssetType type = AssetType::NONE;
        uint64_t key = 0;
        uint64_t shown_key = 0;
        uint64_t failed_key = 0;
        drivers::DeviceDriverVulkan::Image image;
        uint64_t seen_frame = 0;
        double next_check = 0.0;
        bool queued = false;
    };

    struct Job {
        Guid guid;
        AssetType type = AssetType::NONE;
        uint64_t key = 0;
        Guid model;
        Guid mesh;
        std::vector<Guid> materials;
        std::vector<Guid> textures;
        JobState state = JobState::NONE;
        uint64_t frame = 0;
        uint64_t ticket = 0;
        Camera camera;
    };

    struct ParsedMaterial { uint64_t stamp = 0; LMaterial material; };
    struct ParsedModel { uint64_t stamp = 0; LModel model; };
    struct Retired { drivers::DeviceDriverVulkan::Image image; uint64_t frame = 0; };
    struct PendingWrite { JobCounter counter; bool ok = false; };

    drivers::DeviceDriverVulkan* dd = nullptr;

    std::filesystem::path dir;
    bool disk_scanned = false;
    std::unordered_map<Guid, uint64_t, GuidHash, GuidEq> disk;

    std::unordered_map<Guid, Entry, GuidHash, GuidEq> entries;
    std::unordered_map<Guid, ParsedMaterial, GuidHash, GuidEq> materials;
    std::unordered_map<Guid, ParsedModel, GuidHash, GuidEq> models;
    std::deque<Guid> queue;
    Job job;
    uint64_t ticket_serial = 0;
    std::vector<uint8_t> pixels;

    RenderView view;
    ThumbnailRenderPath* path = nullptr;
    World world;
    Entity entity = ENTITY_NULL;
    Guid preview_sphere;
    Guid preview_mesh;

    std::vector<Retired> retired;
    std::vector<std::unique_ptr<PendingWrite>> writes;

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown(EditorContext& ctx);
    void close(EditorContext& ctx);

    VkImageView get(EditorContext& ctx, Guid p_guid, AssetType p_type);
    void update(EditorContext& ctx);

    Error _ensure_view(EditorContext& ctx);
    void _scan_disk();
    std::filesystem::path _disk_path(Guid p_guid, uint64_t p_key) const;
    bool _load_disk(EditorContext& ctx, Guid p_guid, Entry& r_entry, uint64_t p_key);
    void _check_visible(EditorContext& ctx);
    void _start_next(EditorContext& ctx);
    void _render(EditorContext& ctx);
    void _finish(EditorContext& ctx);
    void _swap_image(EditorContext& ctx, Entry& r_entry, drivers::DeviceDriverVulkan::Image p_image, uint64_t p_key);
    void _retire(EditorContext& ctx, drivers::DeviceDriverVulkan::Image& r_image);
    void _collect(EditorContext& ctx, bool p_all);

    bool _resolve(EditorContext& ctx, Guid p_guid, AssetType p_type, Job& r_job);
    Readiness _ready(EditorContext& ctx, const Job& p_job);
    const LMaterial* _parsed_material(EditorContext& ctx, Guid p_guid, uint64_t& r_stamp);
    const LModel* _parsed_model(EditorContext& ctx, Guid p_guid, uint64_t& r_stamp);
    std::filesystem::path _asset_file(EditorContext& ctx, Guid p_guid) const;
    std::filesystem::path _content_file(EditorContext& ctx, Guid p_guid) const;
};

}