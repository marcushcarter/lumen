#include <core/rendering/renderer.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <core/assets/asset_common.h>
#include <core/io/embedded_resource.h>
#include <core/base/cpu_profiler.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace lumen {

using namespace glm;

/***************/
/**** SETUP ****/
/***************/

Error Renderer::_create_dynamic_buffers()
{
    using enum Error;

    instance_buffers.resize(frame_count);
    transform_buffers.resize(frame_count);
    camera_buffers.resize(frame_count);
    light_buffers.resize(frame_count);
    atmosphere_buffers.resize(frame_count);
    
    for (uint32_t i = 0; i < frame_count; i++) {
        instance_buffers[i] = dd->buffer_create({.size = (VkDeviceSize)MAX_INSTANCES * sizeof(Instance),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "instances"});
        transform_buffers[i] = dd->buffer_create({.size = (VkDeviceSize)MAX_INSTANCES * sizeof(Transform),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "transforms"});
        camera_buffers[i] = dd->buffer_create({.size = sizeof(CameraUniform),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "camera"});
        light_buffers[i] = dd->buffer_create({.size = (VkDeviceSize)MAX_LIGHTS * sizeof(GpuLight),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "lights"});
        atmosphere_buffers[i] = dd->buffer_create({.size = sizeof(GpuAtmosphere),.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.device_local = false,.host_visible = true,.pool = dd->bar_pool(),.name = "atmosphere"});
    }

    return OK;
}

void Renderer::_destroy_dynamic_buffers()
{
    for (uint32_t i = 0; i < frame_count; i++) {
        dd->buffer_free(instance_buffers[i]);
        dd->buffer_free(transform_buffers[i]);
        dd->buffer_free(camera_buffers[i]);
        dd->buffer_free(light_buffers[i]);
        dd->buffer_free(atmosphere_buffers[i]);
    }
}

Error Renderer::initialize(drivers::DeviceDriverVulkan& r_dd, const ProfilingSettings& p_profiling)
{
    using enum Error;

    dd = &r_dd;
    profiling = &p_profiling;
    graph.profiler.settings = &p_profiling;
    frame_count = dd->frame_count;
    current_frame = 0;

    in_flight_fences.resize(frame_count);
    image_available_semaphores.resize(frame_count);
    command_pools.resize(frame_count);
    command_buffers.resize(frame_count);

    uint32_t graphics_family = dd->cd->graphics_queue_family;

    for (uint32_t i = 0; i < frame_count; i++) {
        in_flight_fences[i] = dd->fence_create(true);
        image_available_semaphores[i] = dd->semaphore_create();
        command_pools[i] = dd->command_pool_create(graphics_family);
        command_buffers[i] = dd->command_buffer_create(command_pools[i]);
    }
    images_in_flight.assign(dd->swapchain.images.size(), VK_NULL_HANDLE);

    Error err = textures.initialize(r_dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = geometry.initialize(r_dd, frame_count);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = models.initialize(r_dd, frame_count);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = graph.initialize(r_dd, frame_count);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    graph.declare_image_format("Backbuffer", dd->swapchain.format);

    err = _create_dynamic_buffers();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    set_size(1, 1);
    pending_width = width;
    pending_height = height;

    return OK;
}

void Renderer::shutdown()
{
    unload();
    models.shutdown();
    geometry.shutdown();
    
    graph.shutdown();
    _collect_hiz_retired(true);

    _destroy_dynamic_buffers();
    _destroy_hiz_pyramid();

    for (uint32_t i = 0; i < frame_count; i++) {
        dd->fence_free(in_flight_fences[i]);
        dd->semaphore_free(image_available_semaphores[i]);
        dd->command_pool_free(command_pools[i]);
    }
}

/*****************/
/**** PROJECT ****/
/*****************/

Error Renderer::load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir)
{
    using enum Error;
    
    Error err = geometry.allocate();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    hiz_reset_pending = true;

    std::error_code ec;
    if (std::filesystem::exists(p_assets_dir, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(p_assets_dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".lmodel") continue;
            models.load(it->path());
        }
    }

    ec.clear();
    if (!std::filesystem::exists(p_content_dir, ec)) return OK;

    for (auto it = std::filesystem::recursive_directory_iterator(p_content_dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& path = it->path();
        if (path.extension() != ".bin") continue;
        LAssetHeader ah{};
        if (!read_asset_header(path, ah)) continue;
        switch (ah.type) {
            case AssetType::TEXTURE:
                textures.load(ah.guid, path);
                break;
            case AssetType::MESH:
                geometry.load(ah.guid, path);
                break;
            default:
                break;
        }
    }

    return OK;
}

void Renderer::unload()
{
    textures.clear();
    models.clear();
    _last_world = nullptr;
    geometry.free();
    frame.entity_cache.clear();
    frame.static_insts.clear();
    frame.static_lights.clear();
    hiz_reset_pending = true;
}

/****************/
/**** SIZING ****/
/****************/

void Renderer::_create_hiz_pyramid(uint32_t p_width, uint32_t p_height)
{
    const uint32_t hw = (((p_width + 1) / 2) + 63u) & ~63u;
    const uint32_t hh = (((p_height + 1) / 2) + 63u) & ~63u;
    uint32_t mips = 1, m = std::max(hw, hh);
    while (m > 1) { m >>= 1; mips++; }

    drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
    ci.format = VK_FORMAT_R32_SFLOAT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.mip_levels = mips;
    ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
    ci.fixed_width = hw;
    ci.fixed_height = hh;
    ci.pool = dd->image_persistent_pool;
    ci.name = "hiz_pyramid";
    hiz_pyramid = dd->image_create_dedicated(ci, { hw, hh });

    hiz_reset_pending = true;
}

void Renderer::_destroy_hiz_pyramid()
{
    if (hiz_pyramid.image) dd->image_free(hiz_pyramid);
    hiz_pyramid = {};
}

void Renderer::_collect_hiz_retired(bool p_all)
{
    for (size_t i = 0; i < hiz_retired.size();) {
        if (p_all || frame_number >= hiz_retired[i].first + frame_count) {
            dd->image_free(hiz_retired[i].second);
            hiz_retired[i] = std::move(hiz_retired.back());
            hiz_retired.pop_back();
        } else {
            ++i;
        }
    }
}

Error Renderer::set_size(uint32_t p_width, uint32_t p_height)
{
    using enum Error;

    if (p_width == 0 || p_height == 0) return OK;
    if (p_width == width && p_height == height) return OK;
    width = p_width;
    height = p_height;
    resize_epoch++;

    Error err = graph.set_size(p_width, p_height);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    
    if (hiz_pyramid.image) hiz_retired.emplace_back(frame_number, std::move(hiz_pyramid));
    hiz_pyramid = {};
    _create_hiz_pyramid(p_width, p_height);

    return OK;
}

void Renderer::request_size(uint32_t p_width, uint32_t p_height)
{
    pending_width = p_width;
    pending_height = p_height;
}

Error Renderer::apply_pending_size()
{
    if (pending_width == 0 || pending_height == 0) return Error::OK;
    if (pending_width == width && pending_height == height) return Error::OK;

    if (width > 1 && height > 1) {
        if (pending_width != settle_width || pending_height != settle_height) {
            settle_width = pending_width;
            settle_height = pending_height;
            settle_frames = 0;
            return Error::OK;
        }
        if (++settle_frames < RESIZE_SETTLE_FRAMES) return Error::OK;
    }
    return set_size(pending_width, pending_height);
}

/****************/
/**** FRAME ****/
/****************/

static void extract_frustum_planes(const glm::mat4& vp, glm::vec4 out[6])
{
    auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    out[0] = r3 + r0;
    out[1] = r3 - r0;
    out[2] = r3 + r1;
    out[3] = r3 - r1;
    out[4] = r2; // far (reverse-Z, [0,1] depth: z_ndc >= 0)
    out[5] = r3 - r2; // near (z_ndc <= 1)
    for (int i = 0; i < 6; i++) out[i] /= glm::length(glm::vec3(out[i]));
}

glm::mat4 grid_transform(uint32_t iterator, float spacing = 1.0f)
{
    constexpr uint32_t width = 10;
    constexpr uint32_t height = 10;

    uint32_t x = iterator % width;
    uint32_t y = (iterator / width) % height;
    uint32_t z = iterator / (width * height);

    return glm::translate(
        glm::mat4(1.0f),
        glm::vec3(x, y, z) * spacing
    );
}

template <typename Fn> static void _mesh_grid_for_each(const ModelGridComponent& p_grid, const LMesh& p_mesh, uint32_t p_budget, Fn&& p_fn)
{
    const uvec3 n = p_grid.count;
    if ((uint64_t)n.x * n.y * n.z > p_budget) return;
    const float spacing = p_grid.spacing > 0.0f ? p_grid.spacing : p_mesh.bounds_sphere.w * 1.5f;
    const vec3 half = (vec3(n) - 1.0f) * 0.5f;
    for (uint32_t z = 0; z < n.z; z++) {
        for (uint32_t y = 0; y < n.y; y++) {
            for (uint32_t x = 0; x < n.x; x++) p_fn((vec3(x, y, z) - half) * spacing);
        }
    }
}

static GpuLight _light_pack(const TransformComponent& p_xf, const LightComponent& p_light, uint32_t p_ies_index)
{
    const uint32_t ies = p_ies_index & 0xFFFFu;
    GpuLight g{};
    g.radiance = p_light.color * p_light.intensity;
    g.type_flags_ies = ((uint32_t)p_light.type & 0xFu) | (((uint32_t)p_light.flags & 0xFu) << 4) | (ies << 8);
    g.shadow_id = LIGHT_SHADOW_NONE;
    switch (p_light.type) {
        case LightType::SPOT: {
            const float outer = std::clamp(p_light.outer_cone_angle, 0.1f, 89.9f);
            const float inner = std::clamp(p_light.inner_cone_angle, 0.0f, outer);
            g.direction = normalize(p_xf.rotation * vec3(0.0f, 0.0f, -1.0f));
            g.cos_outer = std::cos(radians(outer));
            g.inv_cone_range = 1.0f / std::max(std::cos(radians(inner)) - g.cos_outer, 1e-4f);
        } [[fallthrough]];
        case LightType::POINT:
            g.position = p_xf.position;
            g.range = std::max(p_light.range, 0.01f);
            g.source_radius = std::clamp(p_light.source_radius, 0.0f, g.range);
            g.fade = packHalf2x16(p_light.fade_end > p_light.fade_start ? vec2(p_light.fade_start, p_light.fade_end) : vec2(0.0f));
            break;
    }
    return g;
}

static GpuLight _directional_pack(vec3 p_to_light, vec3 p_illuminance, float p_angular_radius)
{
    constexpr uint32_t DIRECTIONAL_TYPE = 0;
    constexpr uint32_t FLAGS = LightComponent::FLAG_CAST_SHADOWS | LightComponent::FLAG_AFFECTS_GI;
    GpuLight g{};
    g.direction = -p_to_light;
    g.radiance = p_illuminance;
    g.source_radius = p_angular_radius;
    g.type_flags_ies = DIRECTIONAL_TYPE | (FLAGS << 4) | (LIGHT_IES_NONE << 8);
    g.shadow_id = LIGHT_SHADOW_NONE;
    return g;
}

static float _cycle_ev100(float p_sun_altitude_deg)
{
    static constexpr float KEYS[][2] = { {-90.0f, -2.5f}, {-18.0f, -2.5f}, {-12.0f, -1.0f}, {-9.0f, 1.5f}, {-6.0f, 5.6f}, {-3.0f, 8.8f}, {0.0f, 10.9f}, {2.0f, 11.4f}, {8.0f, 13.0f}, {20.0f, 14.1f}, {90.0f, 14.8f}};
    constexpr int COUNT = (int)(sizeof(KEYS) / sizeof(KEYS[0]));
    const float e = std::clamp(p_sun_altitude_deg, -90.0f, 90.0f);
    
    auto slope = [](int p_k) { return (KEYS[p_k + 1][1] - KEYS[p_k][1]) / (KEYS[p_k + 1][0] - KEYS[p_k][0]); };
    auto tangent = [&](int p_k) {
        if (p_k == 0) return slope(0);
        if (p_k == COUNT - 1) return slope(COUNT - 2);
        const float d0 = slope(p_k - 1);
        const float d1 = slope(p_k);
        if (d0 * d1 <= 0.0f) return 0.0f;
        const float h0 = KEYS[p_k][0] - KEYS[p_k - 1][0];
        const float h1 = KEYS[p_k + 1][0] - KEYS[p_k][0];
        return 3.0f * (h0 + h1) / ((2.0f * h1 + h0) / d0 + (h1 + 2.0f * h0) / d1);
    };
    int i = 0;
    while (i < COUNT - 2 && e > KEYS[i + 1][0]) i++;
    const float h = KEYS[i + 1][0] - KEYS[i][0];
    const float t = (e - KEYS[i][0]) / h;
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * KEYS[i][1] + (t3 - 2.0f * t2 + t) * h * tangent(i) + (-2.0f * t3 + 3.0f * t2) * KEYS[i + 1][1] + (t3 - t2) * h * tangent(i + 1);
}

static uint32_t _ref_capacity_add(uint32_t p_a, uint32_t p_b)
{
    return (uint32_t)std::min<uint64_t>((uint64_t)p_a + p_b, MAX_CLUSTER_REFS);
}

static bool _model_sane(const mat4& p_m)
{
    constexpr float LIMIT = 1e7f;
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) if (!std::isfinite(p_m[c][r]) || std::abs(p_m[c][r]) > LIMIT) return false;
    return true;
}

void Renderer::_frame_build(const World& p_world)
{
    frame.reset();
    models.resolve(geometry);

    StaticInstances& static_insts = frame.static_insts;
    if (static_insts.world_version != p_world.static_version || static_insts.geometry_version != geometry.version || static_insts.model_version != models.version) {
        static_insts.instances.clear();
        static_insts.transforms.clear();
        static_insts.cluster_ref_capacity = 0;
        p_world.view<TransformComponent, ModelComponent, StaticTag>(Exclude<EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent& p_xf, const ModelComponent& p_model, const StaticTag&) {
            if (static_insts.instances.size() >= MAX_INSTANCES) return;
            uint32_t mesh_index, slot_base;
            models.lookup(p_model.model, mesh_index, slot_base);
            const LMesh* mesh = geometry.get(mesh_index);
            if (!mesh) return;
            const mat4 model = translate(mat4(1.0f), p_xf.position) * mat4_cast(p_xf.rotation) * scale(mat4(1.0f), p_xf.scale);
            if (!_model_sane(model)) return;
            static_insts.instances.push_back(Instance{ mesh_index, (uint32_t)static_insts.transforms.size(), p_entity.index, slot_base });
            static_insts.transforms.push_back(Transform{ model, model });
            static_insts.cluster_ref_capacity = _ref_capacity_add(static_insts.cluster_ref_capacity, mesh->cluster_count);
        });
        p_world.view<TransformComponent, ModelGridComponent, StaticTag>(Exclude<EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent& p_xf, const ModelGridComponent& p_grid, const StaticTag&) {
            uint32_t mesh_index, slot_base;
            models.lookup(p_grid.model, mesh_index, slot_base);
            const LMesh* mesh = geometry.get(mesh_index);
            if (!mesh) return;
            const mat4 root = translate(mat4(1.0f), p_xf.position) * mat4_cast(p_xf.rotation) * scale(mat4(1.0f), p_xf.scale);
            if (!_model_sane(root)) return;
            _mesh_grid_for_each(p_grid, *mesh, MAX_INSTANCES - (uint32_t)static_insts.instances.size(), [&](vec3 p_offset) {
                const mat4 model = root * translate(mat4(1.0f), p_offset);
                static_insts.instances.push_back(Instance{ mesh_index, (uint32_t)static_insts.transforms.size(), p_entity.index, slot_base });
                static_insts.transforms.push_back(Transform{ model, model });
                static_insts.cluster_ref_capacity = _ref_capacity_add(static_insts.cluster_ref_capacity, mesh->cluster_count);
            });
        });
        static_insts.world_version = p_world.static_version;
        static_insts.geometry_version = geometry.version;
        static_insts.model_version = models.version;
        static_insts.version++;
    }

    const uint32_t base = static_insts.count();
    p_world.view<TransformComponent, ModelComponent>(Exclude<StaticTag, EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent& p_xf, const ModelComponent& p_model) {
        if (base + frame.instances_scratch.size() >= MAX_INSTANCES) return;
        if (p_entity.index >= frame.entity_cache.size()) frame.entity_cache.resize(p_entity.index + 1);
        FrameData::EntityCache& cache = frame.entity_cache[p_entity.index];
        if (cache.generation != p_entity.generation) cache = { mat4(1.0f), frame_number, p_entity.generation, GeometryPool::INVALID_MESH };
        if (cache.model != p_model.model || cache.model_version != models.version) {
            cache.model = p_model.model;
            cache.model_version = models.version;
            models.lookup(p_model.model, cache.mesh_index, cache.slot_base);
        }
        const LMesh* mesh = geometry.get(cache.mesh_index);
        if (!mesh) return;
        const mat4 model = translate(mat4(1.0f), p_xf.position) * mat4_cast(p_xf.rotation) * scale(mat4(1.0f), p_xf.scale);
        if (!_model_sane(model)) return;
        const mat4 prev_mtx = cache.frame + 1 == frame_number ? cache.prev_mtx : model;
        cache.prev_mtx = model;
        cache.frame = frame_number;
        frame.instances_scratch.push_back(Instance{ cache.mesh_index, base + (uint32_t)frame.transforms_scratch.size(), p_entity.index, cache.slot_base });
        frame.transforms_scratch.push_back(Transform{ prev_mtx, model });
        frame.cluster_ref_capacity = _ref_capacity_add(frame.cluster_ref_capacity, mesh->cluster_count);
    });

    p_world.view<TransformComponent, ModelGridComponent>(Exclude<StaticTag, EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent& p_xf, const ModelGridComponent& p_grid) {
        if (p_entity.index >= frame.entity_cache.size()) frame.entity_cache.resize(p_entity.index + 1);
        FrameData::EntityCache& cache = frame.entity_cache[p_entity.index];
        if (cache.generation != p_entity.generation) cache = { mat4(1.0f), frame_number, p_entity.generation, GeometryPool::INVALID_MESH };
        if (cache.model != p_grid.model || cache.model_version != models.version) {
            cache.model = p_grid.model;
            cache.model_version = models.version;
            models.lookup(p_grid.model, cache.mesh_index, cache.slot_base);
        }
        const LMesh* mesh = geometry.get(cache.mesh_index);
        if (!mesh) return;
        const mat4 root = translate(mat4(1.0f), p_xf.position) * mat4_cast(p_xf.rotation) * scale(mat4(1.0f), p_xf.scale);
        if (!_model_sane(root)) return;
        const mat4 prev_root = cache.frame + 1 == frame_number ? cache.prev_mtx : root;
        cache.prev_mtx = root;
        cache.frame = frame_number;
        _mesh_grid_for_each(p_grid, *mesh, MAX_INSTANCES - base - (uint32_t)frame.instances_scratch.size(), [&](vec3 p_offset) {
            const mat4 offset = translate(mat4(1.0f), p_offset);
            frame.instances_scratch.push_back(Instance{ cache.mesh_index, base + (uint32_t)frame.transforms_scratch.size(), p_entity.index, cache.slot_base });
            frame.transforms_scratch.push_back(Transform{ prev_root * offset, root * offset });
            frame.cluster_ref_capacity = _ref_capacity_add(frame.cluster_ref_capacity, mesh->cluster_count);
        });
    });

    frame.instance_count = base + (uint32_t)frame.instances_scratch.size();
    frame.cluster_ref_capacity = _ref_capacity_add(frame.cluster_ref_capacity, static_insts.cluster_ref_capacity);

    StaticLights& static_lights = frame.static_lights;
    if (static_lights.world_version != p_world.static_version) {
        static_lights.lights.clear();
        p_world.view<TransformComponent, LightComponent, StaticTag>(Exclude<EditorHiddenTag>{}, [&](Entity, const TransformComponent& p_xf, const LightComponent& p_light, const StaticTag&) {
            if (static_lights.lights.size() < MAX_LIGHTS - MAX_DIRECTIONAL_LIGHTS) static_lights.lights.push_back(_light_pack(p_xf, p_light, 0xFFFF));
        });
        static_lights.world_version = p_world.static_version;
        static_lights.version++;
    }

    const uint32_t light_base = MAX_DIRECTIONAL_LIGHTS + static_lights.count();
    p_world.view<TransformComponent, LightComponent>(Exclude<StaticTag, EditorHiddenTag>{}, [&](Entity, const TransformComponent& p_xf, const LightComponent& p_light) {
        if (light_base + frame.lights_scratch.size() < MAX_LIGHTS) {
            frame.lights_scratch.push_back(_light_pack(p_xf, p_light, 0xFFFF));
        }
    });
    frame.light_count = light_base + (uint32_t)frame.lights_scratch.size();

    // Camera.
    {
        const float aspect = height ? (float)width / (float)height : 1.0f;
        const mat4 prev_vp = frame.camera.curr_view_proj;
        frame.camera.curr_view_proj = active_camera.view_proj(aspect);
        frame.camera.prev_view_proj = camera_cut_pending ? frame.camera.curr_view_proj : prev_vp;
        frame.camera.position = vec4(active_camera.position, 1.0f);
        extract_frustum_planes(frame.camera.curr_view_proj, frame.camera.frustum_planes);
        frame.camera.near_z = active_camera.near_z;
        frame.camera.far_z = active_camera.far_z;
        frame.camera.tan_half_fov_y = std::tan(active_camera.fov_y * 0.5f);
        frame.camera.inv_view_proj = inverse(frame.camera.curr_view_proj);
        frame.px_per_unit = 0.5f * (float)height / std::tan(active_camera.fov_y * 0.5f) * lod_bias;
        frame.hiz_history_valid = !camera_cut_pending && !hiz_reset_pending;
    }

    // Atmosphere.
    {
        using A = AtmosphereComponent;
        const AtmosphereComponent& atmo = p_world.atmosphere;
        GpuAtmosphere& g = frame.atmosphere;

        const vec4 lut_key[2] = { vec4(atmo.mie_turbidity, atmo.mie_height_km, atmo.mie_albedo, atmo.mie_angstrom), vec4(atmo.strat_aerosol_depth, atmo.strat_aerosol_height_km, 0.0f, 0.0f) };
        if (lut_key[0] != frame.atmosphere_lut_key[0] || lut_key[1] != frame.atmosphere_lut_key[1]) {
            frame.atmosphere_lut_key[0] = lut_key[0];
            frame.atmosphere_lut_key[1] = lut_key[1];
            frame.atmosphere_lut_version++;
        }

        const vec3 angstrom = pow(vec3(680.0f, 550.0f, 440.0f) / 550.0f, vec3(-atmo.mie_angstrom));
        const vec3 mie_scattering = A::MIE_BASE_SCATTERING * atmo.mie_turbidity * angstrom;
        g.rayleigh_scattering = A::RAYLEIGH_SCATTERING;
        g.rayleigh_density_scale = -1.0f / A::RAYLEIGH_HEIGHT_KM;
        g.mie_scattering = mie_scattering;
        g.mie_density_scale = -1.0f / max(atmo.mie_height_km, 0.01f);
        g.mie_extinction = mie_scattering / max(atmo.mie_albedo, 0.01f);
        g.mie_g = atmo.mie_g;
        g.ozone_absorption = A::OZONE_ABSORPTION;
        g.ozone_center_km = A::OZONE_CENTER_KM;
        g.ground_albedo = A::GROUND_ALBEDO;
        g.ozone_width_km = A::OZONE_WIDTH_KM;
        const vec3 strat_angstrom = pow(vec3(680.0f, 550.0f, 440.0f) / 550.0f, vec3(-A::STRAT_AEROSOL_ANGSTROM));
        g.strat_scattering = strat_angstrom * (atmo.strat_aerosol_depth / (0.5f * A::STRAT_AEROSOL_WIDTH_KM));
        g.strat_center_km = atmo.strat_aerosol_height_km;
        g.strat_width_km = A::STRAT_AEROSOL_WIDTH_KM;
        g.strat_g = A::STRAT_AEROSOL_G;
        g.bottom_radius_km = A::PLANET_RADIUS_KM;
        g.top_radius_km = A::PLANET_RADIUS_KM + A::ATMOSPHERE_HEIGHT_KM;

        const double PI_D = 3.14159265358979323846;
        const double DEG = PI_D / 180.0;
        const double lat = A::LATITUDE_DEG * DEG;
        const double rotation = ((double)atmo.time_of_day - 12.0) * 15.0 * DEG;
        const double ch = std::cos(rotation), sh = std::sin(rotation);
        const double cp = std::cos(lat), sp = std::sin(lat);
        const dmat3 hour(ch, -sh, 0.0, sh, ch, 0.0, 0.0, 0.0, 1.0);
        const dmat3 local(0.0, cp, sp, 1.0, 0.0, 0.0, 0.0, sp, -cp);
        const dmat3 eq_to_world = local * hour;
        auto radec = [](double p_ra, double p_dec) { return dvec3(std::cos(p_dec) * std::cos(p_ra), std::cos(p_dec) * std::sin(p_ra), std::sin(p_dec)); };
        const dvec3 sun = normalize(eq_to_world * radec(0.0, A::SUN_DECLINATION_DEG * DEG));
        const dvec3 moon = normalize(eq_to_world * radec(PI_D, (-A::SUN_DECLINATION_DEG + A::MOON_OFFSET_DEG) * DEG));
        const dvec3 pole = eq_to_world * dvec3(0.0, 0.0, 1.0);
        const double phase_angle = PI_D - std::acos(clamp(dot(sun, moon), -1.0, 1.0));
        const double phase_deg = phase_angle / DEG;
        const double moon_mag = -12.73 + 0.026 * phase_deg + 4e-9 * std::pow(phase_deg, 4.0);
        const float moon_lux = (float)(2.54e-6 * std::pow(10.0, -0.4 * moon_mag));

        g.world_to_celestial = mat4(transpose(mat3(eq_to_world)));
        g.sun_direction = vec3(sun);
        g.sun_cos_angular_radius = std::cos(A::SUN_ANGULAR_RADIUS);
        g.sun_illuminance = vec3(A::SUN_ILLUMINANCE_LUX);
        g.moon_direction = vec3(moon);
        g.moon_cos_angular_radius = std::cos(A::MOON_ANGULAR_RADIUS);
        g.moon_illuminance = vec3(moon_lux);
        g.moon_up = normalize(vec3(pole - moon * dot(pole, moon)));
        g.moon_phase_angle = (float)phase_angle;

        const vec3 cam_km = vec3(frame.camera.position) * 0.001f;
        const float altitude = max(cam_km.y + atmo.sea_level_offset_km, 0.001f);
        g.observer_km = vec3(cam_km.x, A::PLANET_RADIUS_KM + altitude, cam_km.z);
        g.altitude_km = altitude;
        g.night_luminance = A::NIGHT_LUMINANCE;
        g.star_brightness = atmo.star_brightness;
        g.star_density = atmo.star_density;
        g.moon_disk_brightness = A::MOON_DISK_BRIGHTNESS;
        g.sun_angular_radius = A::SUN_ANGULAR_RADIUS;
        g.moon_angular_radius = A::MOON_ANGULAR_RADIUS;
        {
            const double r = (double)A::PLANET_RADIUS_KM + altitude;
            const double rg = A::PLANET_RADIUS_KM;
            const double v_horizon = std::sqrt(std::max(r * r - rg * rg, 0.0));
            const double beta = std::acos(clamp(v_horizon / r, -1.0, 1.0));
            g.skyview_beta = (float)beta;
            g.skyview_zenith_horizon = (float)(PI_D - beta);
            g.horizon_mu = (float)-std::sqrt(std::max(1.0 - (rg / r) * (rg / r), 0.0));
        }

        frame.directional[0] = _directional_pack(g.sun_direction, g.sun_illuminance, A::SUN_ANGULAR_RADIUS);
        frame.directional[1] = _directional_pack(g.moon_direction, g.moon_illuminance, A::MOON_ANGULAR_RADIUS);
        frame.directional_count = MAX_DIRECTIONAL_LIGHTS;

        const float sun_altitude_deg = (float)(std::asin(clamp(sun.y, -1.0, 1.0)) / DEG);
        frame.camera.pre_exposure = 1.0f / (1.2f * std::exp2(_cycle_ev100(sun_altitude_deg)));
    }
    
    camera_cut_pending = false;
    hiz_reset_pending = false;
}

void Renderer::_frame_upload()
{
    StaticInstances& static_insts = frame.static_insts;
    drivers::DeviceDriverVulkan::Buffer& ib = instance_buffers[current_frame];
    drivers::DeviceDriverVulkan::Buffer& tb = transform_buffers[current_frame];
    const uint32_t base = static_insts.count();
    if (static_insts.uploaded_version.size() != frame_count) static_insts.uploaded_version.assign(frame_count, UINT64_MAX);

    if (static_insts.uploaded_version[current_frame] != static_insts.version) {
        if (base != 0) {
            dd->buffer_update(ib, static_insts.instances.data(), (VkDeviceSize)base * sizeof(Instance));
            dd->buffer_flush(ib, 0, (VkDeviceSize)base * sizeof(Instance));
            dd->buffer_update(tb, static_insts.transforms.data(), (VkDeviceSize)base * sizeof(Transform));
            dd->buffer_flush(tb, 0, (VkDeviceSize)base * sizeof(Transform));
        }
        static_insts.uploaded_version[current_frame] = static_insts.version;
    }
    
    const uint32_t dynamic_count = frame.instance_count - base;
    if (dynamic_count != 0) {
        const VkDeviceSize i_off = (VkDeviceSize)base * sizeof(Instance);
        const VkDeviceSize t_off = (VkDeviceSize)base * sizeof(Transform);
        dd->buffer_update(ib, frame.instances_scratch.data(), (VkDeviceSize)dynamic_count * sizeof(Instance), i_off);
        dd->buffer_flush(ib, i_off, (VkDeviceSize)dynamic_count * sizeof(Instance));
        dd->buffer_update(tb, frame.transforms_scratch.data(), (VkDeviceSize)dynamic_count * sizeof(Transform), t_off);
        dd->buffer_flush(tb, t_off, (VkDeviceSize)dynamic_count * sizeof(Transform));
    }

    StaticLights& static_lights = frame.static_lights;
    drivers::DeviceDriverVulkan::Buffer& lb = light_buffers[current_frame];
    if (static_lights.uploaded_version.size() != frame_count) static_lights.uploaded_version.assign(frame_count, UINT64_MAX);

    dd->buffer_update(lb, frame.directional, sizeof(frame.directional));
    dd->buffer_flush(lb, 0, sizeof(frame.directional));

    const uint32_t light_base = MAX_DIRECTIONAL_LIGHTS + static_lights.count();
    if (static_lights.uploaded_version[current_frame] != static_lights.version) {
        if (static_lights.count() != 0) {
            const VkDeviceSize s_off = (VkDeviceSize)MAX_DIRECTIONAL_LIGHTS * sizeof(GpuLight);
            dd->buffer_update(lb, static_lights.lights.data(), (VkDeviceSize)static_lights.count() * sizeof(GpuLight), s_off);
            dd->buffer_flush(lb, s_off, (VkDeviceSize)static_lights.count() * sizeof(GpuLight));
        }
        static_lights.uploaded_version[current_frame] = static_lights.version;
    }

    const uint32_t dynamic_lights = frame.light_count - light_base;
    if (dynamic_lights != 0) {
        const VkDeviceSize l_off = (VkDeviceSize)light_base * sizeof(GpuLight);
        dd->buffer_update(lb, frame.lights_scratch.data(), (VkDeviceSize)dynamic_lights * sizeof(GpuLight), l_off);
        dd->buffer_flush(lb, l_off, (VkDeviceSize)dynamic_lights * sizeof(GpuLight));
    }
    
    drivers::DeviceDriverVulkan::Buffer& cb = camera_buffers[current_frame];
    dd->buffer_update(cb, &frame.camera, sizeof(CameraUniform));
    dd->buffer_flush(cb, 0, sizeof(CameraUniform));

    drivers::DeviceDriverVulkan::Buffer& ab = atmosphere_buffers[current_frame];
    dd->buffer_update(ab, &frame.atmosphere, sizeof(GpuAtmosphere));
    dd->buffer_flush(ab, 0, sizeof(GpuAtmosphere));

    models.upload(current_frame);
}

Error Renderer::acquire_frame()
{
    using enum Error;
    
    auto& sc = dd->swapchain;
    CpuProfiler& cpu = cpu_profiler();

    if (sc.generation != swapchain_generation) {
        swapchain_generation = sc.generation;
        graph.framebuffers_flush();
        images_in_flight.assign(sc.images.size(), VK_NULL_HANDLE);
    }

    cpu.zone_begin("Frame Fence", CpuProfiler::FLAG_WAIT);
    Error err = dd->fence_wait(in_flight_fences[current_frame]);
    cpu.zone_end();
    if (err != OK) graph.breadcrumb_report();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    cpu.zone_begin("Swapchain Acquire", CpuProfiler::FLAG_WAIT);
    err = dd->swapchain_acquire_next_image(image_available_semaphores[current_frame]);
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    if (sc.image_index == UINT32_MAX) return OK;

    if (images_in_flight[sc.image_index] != VK_NULL_HANDLE) {
        cpu.zone_begin("Image Fence", CpuProfiler::FLAG_WAIT);
        dd->fence_wait(images_in_flight[sc.image_index]);
        cpu.zone_end();
    }
    images_in_flight[sc.image_index] = in_flight_fences[current_frame];

    frame_acquired = true;
    return OK;
}

Error Renderer::begin_frame(const World& p_world)
{
    using enum Error;
    
    auto& sc = dd->swapchain;
    CpuProfiler& cpu = cpu_profiler();

    if (!frame_acquired) {
        Error err = acquire_frame();
        LUMEN_ERR_FAIL_COND_V(err != OK, err);
        LUMEN_ERR_FAIL_COND_V(!frame_acquired, FAILED);
    }

    _collect_hiz_retired(false);
    textures.begin_frame(frame_number, frame_count);
    geometry.begin_frame(frame_number, current_frame, models.slot_table_address(current_frame));

    if (&p_world != _last_world) {
        _last_world = &p_world;
        frame.entity_cache.clear();
        frame.static_insts.clear();
        frame.static_lights.clear();
        hiz_reset_pending = true;
        camera_cut_pending = true;
    }

    cpu.zone_begin("Scene Gather");
    _frame_build(p_world);
    cpu.zone_end();

    cpu.zone_begin("Upload");
    _frame_upload();
    cpu.zone_end();

    graph.begin(current_frame);
    graph.import_image("Backbuffer", &sc.images[sc.image_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_image("HiZ", &hiz_pyramid, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);

    graph.import_buffer("Camera", &camera_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Atmosphere", &atmosphere_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Geometry", &geometry.address_buffer(current_frame), VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Instances", &instance_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Transforms", &transform_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    graph.import_buffer("Lights", &light_buffers[current_frame], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    return OK;
}

Error Renderer::compile()
{
    cpu_profiler().zone_begin("Graph Compile");
    const Error err = graph.compile();
    cpu_profiler().zone_end();
    return err;
}

Error Renderer::record()
{
    using enum Error;

    Error err = dd->command_pool_reset(command_pools[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = dd->command_buffer_begin(command_buffers[current_frame], VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    
    VkCommandBuffer cmd = command_buffers[current_frame];

    dd->command_bind_graphics_uniform_sets(cmd, { dd->bindless_heap.set });
    dd->command_bind_compute_uniform_sets(cmd, { dd->bindless_heap.set });

    cpu_profiler().zone_begin("Graph Execute");
    graph.execute(cmd);
    cpu_profiler().zone_end();
    
    err = dd->command_buffer_end(command_buffers[current_frame]);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    return OK;
}

Error Renderer::end_frame()
{
    using enum Error;
    
    auto& sc = dd->swapchain;
    CpuProfiler& cpu = cpu_profiler();

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available_semaphores[current_frame];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[current_frame];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &sc.present_semaphores[sc.image_index];

    VkQueue graphics_queue = dd->queue_families[dd->cd->graphics_queue_family][0].queue;
    Error ferr = dd->fence_reset(in_flight_fences[current_frame]);
    LUMEN_ERR_FAIL_COND_V(ferr != OK, ferr);
    cpu.zone_begin("Queue Submit");
    VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]);
    cpu.zone_end();
    if (result != VK_SUCCESS) {
        log_write("Renderer: vkQueueSubmit returned %d", (int)result);
        graph.breadcrumb_report();
    }
    LUMEN_ERR_FAIL_COND_V_MSG(result != VK_SUCCESS, FAILED, "Failed to submit Vulkan queue");

    VkPresentInfoKHR present_info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &sc.present_semaphores[sc.image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &sc.swapchain;
    present_info.pImageIndices = &sc.image_index;

    VkQueue present_queue = dd->queue_families[dd->cd->present_queue_family][0].queue;
    cpu.zone_begin("Queue Present");
    result = vkQueuePresentKHR(present_queue, &present_info);
    cpu.zone_end();
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        dd->swapchain.surface->needs_resize = true;
    } else {
        if (result == VK_ERROR_DEVICE_LOST) graph.breadcrumb_report();
        LUMEN_ERR_FAIL_COND_V_MSG(result != VK_SUCCESS, FAILED, "Failed to present Vulkan queue");
    }

    current_frame = (current_frame + 1) % frame_count;
    frame_number++;
    frame_acquired = false;

    return OK;
}

RenderContext Renderer::make_context()
{
    RenderContext ctx{};
    ctx.dd = dd;
    ctx.graph = &graph;
    ctx.textures = &textures;
    ctx.geometry = &geometry;
    ctx.frame = &frame;
    ctx.profiling = profiling;
    return ctx;
}

}