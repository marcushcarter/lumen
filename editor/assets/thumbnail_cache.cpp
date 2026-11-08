#include <editor/assets/thumbnail_cache.h>
#include <editor/assets/texture_viewer.h>
#include <editor/assets/builtin_meshes.h>
#include <editor/assets/asset_registry.h>
#include <core/animation/clip_player.h>
#include <toml++/toml.hpp>
#include <editor/application/editor_context.h>
#include <core/base/hash.h>
#include <core/rendering/renderer.h>
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
#include <editor/ui/ui_style.h>
#include <cfloat>
#include <core/world/components.h>
#include <core/project/project.h>
#include <core/io/image_io.h>
#include <core/io/path.h>
#include <drivers/imgui/imgui_driver.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lumen {

static constexpr uint64_t STAMP_MISSING = 0x6D697373696E6721ull;

static uint64_t _mix(uint64_t p_h, uint64_t p_v)
{
    return hash_combine(p_h, hash_splitmix(p_v));
}

static uint64_t _stamp(const std::filesystem::path& p_path)
{
    if (p_path.empty()) return STAMP_MISSING;
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(p_path, ec);
    if (ec) return STAMP_MISSING;
    const uintmax_t size = std::filesystem::file_size(p_path, ec);
    return _mix((uint64_t)t.time_since_epoch().count(), ec ? 0 : (uint64_t)size);
}

static void _texture_refs(const LMaterial& p_material, std::vector<Guid>& r_out)
{
    const Guid g = p_material.mask_texture;
    if (g.valid() && std::find(r_out.begin(), r_out.end(), g) == r_out.end()) r_out.push_back(g);
}

/***************/
/**** SETUP ****/
/***************/

Error ThumbnailCache::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    dd = &r_dd;
    preview_sphere = Guid::generate();
    preview_mesh = Guid::generate();
    return Error::OK;
}

void ThumbnailCache::shutdown(EditorContext& ctx)
{
    close(ctx);
    for (std::unique_ptr<PendingWrite>& w : writes) job_system().wait(w->counter);
    writes.clear();
}

void ThumbnailCache::close(EditorContext& ctx)
{
    if (dd) dd->device_wait_idle();
    for (auto& [guid, e] : entries) {
        if (!e.image.image) continue;
        if (ctx.imgui && ctx.renderer) ctx.imgui->texture_cache.retire(e.image.image_view, ctx.renderer->frame_number, ctx.renderer->frame_count);
        dd->image_free(e.image);
    }
    entries.clear();
    for (auto& [guid, t] : texture_views) {
        if (ctx.imgui && ctx.renderer) ctx.imgui->texture_cache.retire(t.view, ctx.renderer->frame_number, ctx.renderer->frame_count);
        vkDestroyImageView(dd->device, t.view, nullptr);
    }
    texture_views.clear();
    _collect(ctx, true);
    queue.clear();
    job = {};
    materials.clear();
    models.clear();

    if (ctx.renderer) {
        ctx.renderer->models.unload(preview_sphere);
        ctx.renderer->models.unload(preview_mesh);
    }
    if (path) {
        path->destroy_resources();
        delete path;
        path = nullptr;
        render_view.path = nullptr;
        if (ctx.renderer) ctx.renderer->view_destroy(render_view);
        world.shutdown();
        entity = ENTITY_NULL;
    }
}

Error ThumbnailCache::_ensure_view(EditorContext& ctx)
{
    using enum Error;
    if (path) return OK;
    Renderer& r = *ctx.renderer;

    Error err = r.view_create(render_view, RENDER_SIZE, RENDER_SIZE);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    path = new ThumbnailRenderPath();
    path->ctx = r.make_context(render_view);
    err = path->create_resources();
    if (err != OK) {
        delete path;
        path = nullptr;
        r.view_destroy(render_view);
        log_write("ThumbnailCache: render path creation failed.");
        return err;
    }
    render_view.path = path;

    world.initialize();
    world.load();
    entity = world.create();
    world.add<TransformComponent>(entity);
    world.add<ModelComponent>(entity, { preview_sphere });
    world.add<PoseComponent>(entity);
    return OK;
}

/**************/
/**** DISK ****/
/**************/

void ThumbnailCache::_scan_disk()
{
    disk_scanned = true;
    dir = Paths::local_data(L"thumbnails");
    if (dir.empty()) return;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".png") continue;
        const std::string stem = it->path().stem().string();
        if (stem.size() != Guid::CHARS * 2 + 1 || stem[Guid::CHARS] != '_') continue;
        const Guid guid = Guid::from_string(std::string_view(stem).substr(0, Guid::CHARS));
        const uint64_t key = std::strtoull(stem.c_str() + Guid::CHARS + 1, nullptr, 16);
        if (!guid.valid()) continue;
        disk[guid] = key;
    }
}

std::filesystem::path ThumbnailCache::_disk_path(Guid p_guid, uint64_t p_key) const
{
    char guid[Guid::BUFFER];
    p_guid.to_chars(guid);
    char name[64];
    std::snprintf(name, sizeof(name), "%s_%016llx.png", guid, (unsigned long long)p_key);
    return dir / name;
}

bool ThumbnailCache::_load_disk(EditorContext& ctx, Guid p_guid, Entry& r_entry, uint64_t p_key)
{
    ImageData<uint8_t, 4> img = ImageIO::load_from_file<uint8_t, 4>(_disk_path(p_guid, p_key).wstring());
    const bool ok = img.valid() && img.width == (int)OUTPUT_SIZE && img.height == (int)OUTPUT_SIZE;
    if (ok) _swap_image(ctx, r_entry, dd->image_create_texture(img.pixels, OUTPUT_SIZE, OUTPUT_SIZE, "thumbnail"), p_key);
    ImageIO::free_image(img);
    if (!ok) disk.erase(p_guid);
    return ok;
}

/****************/
/**** IMAGES ****/
/****************/

void ThumbnailCache::_swap_image(EditorContext& ctx, Entry& r_entry, drivers::DeviceDriverVulkan::Image p_image, uint64_t p_key)
{
    if (!p_image.image) return;
    _retire(ctx, r_entry.image);
    r_entry.image = p_image;
    r_entry.shown_key = p_key;
}

void ThumbnailCache::_retire(EditorContext& ctx, drivers::DeviceDriverVulkan::Image& r_image)
{
    if (!r_image.image) return;
    ctx.imgui->texture_cache.retire(r_image.image_view, ctx.renderer->frame_number, ctx.renderer->frame_count);
    retired.push_back({ r_image, ctx.renderer->frame_number });
    r_image = {};
}

void ThumbnailCache::_collect(EditorContext& ctx, bool p_all)
{
    const uint64_t frame = ctx.renderer ? ctx.renderer->frame_number : 0;
    const uint32_t frames = ctx.renderer ? ctx.renderer->frame_count : 0;
    for (size_t i = 0; i < retired.size();) {
        if (p_all || frame >= retired[i].frame + frames) {
            dd->image_free(retired[i].image);
            retired[i] = std::move(retired.back());
            retired.pop_back();
        } else {
            ++i;
        }
    }
    if (ctx.renderer && !p_all) {
        for (auto it = texture_views.begin(); it != texture_views.end();) {
            const LTexture* tex = ctx.renderer->textures.get(it->first);
            if (tex && tex->image.image == it->second.image) {
                ++it;
                continue;
            }
            _retire_view(ctx, it->second.view);
            it = texture_views.erase(it);
        }
    }
    for (size_t i = 0; i < retired_views.size();) {
        if (p_all || frame >= retired_views[i].frame + frames) {
            vkDestroyImageView(dd->device, retired_views[i].view, nullptr);
            retired_views[i] = retired_views.back();
            retired_views.pop_back();
        } else {
            ++i;
        }
    }
    for (size_t i = 0; i < writes.size();) {
        if (!writes[i]->counter.done()) {
            ++i;
            continue;
        }
        if (!writes[i]->ok) log_write("ThumbnailCache: failed to write a thumbnail to disk.");
        writes[i] = std::move(writes.back());
        writes.pop_back();
    }
}

/*************/
/**** KEY ****/
/*************/

void ThumbnailCache::_retire_view(EditorContext& ctx, VkImageView p_view)
{
    ctx.imgui->texture_cache.retire(p_view, ctx.renderer->frame_number, ctx.renderer->frame_count);
    retired_views.push_back({ p_view, ctx.renderer->frame_number });
}

VkImageView ThumbnailCache::_texture_view(EditorContext& ctx, Guid p_guid)
{
    const LTexture* tex = ctx.renderer->textures.get(p_guid);
    if (!tex) return VK_NULL_HANDLE;
    const drivers::DeviceDriverVulkan::Image& image = tex->image;
    const uint32_t channels = TextureViewer::channel_count(image.format);
    if (channels > 2) return image.image_view;

    auto it = texture_views.find(p_guid);
    if (it != texture_views.end()) {
        if (it->second.image == image.image) return it->second.view;
        _retire_view(ctx, it->second.view);
        texture_views.erase(it);
    }

    VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ci.image = image.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = image.format;
    const VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_R;
    const VkComponentSwizzle one = VK_COMPONENT_SWIZZLE_ONE;
    ci.components = channels == 1 ? VkComponentMapping{ r, r, r, one } : VkComponentMapping{ r, VK_COMPONENT_SWIZZLE_G, one, one };
    ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mip_levels, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(dd->device, &ci, nullptr, &view) != VK_SUCCESS) return image.image_view;
    texture_views[p_guid] = TextureView{ image.image, view };
    return view;
}

std::filesystem::path ThumbnailCache::_asset_file(EditorContext& ctx, Guid p_guid) const
{
    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(p_guid) : nullptr;
    return e ? e->path : std::filesystem::path{};
}

std::filesystem::path ThumbnailCache::_content_file(EditorContext& ctx, Guid p_guid) const
{
    return ctx.project ? ctx.project->content_path(p_guid) : std::filesystem::path{};
}

const LMaterial* ThumbnailCache::_parsed_material(EditorContext& ctx, Guid p_guid, uint64_t& r_stamp)
{
    const std::filesystem::path file = _asset_file(ctx, p_guid);
    r_stamp = _stamp(file);
    if (r_stamp == STAMP_MISSING) return nullptr;
    ParsedMaterial& p = materials[p_guid];
    if (p.stamp != r_stamp) {
        p.stamp = r_stamp;
        if (p.material.load(file) != Error::OK) p.material = LMaterial{};
    }
    return p.material.guid == p_guid ? &p.material : nullptr;
}

const ThumbnailCache::ParsedAnimation* ThumbnailCache::_parsed_animation(EditorContext& ctx, Guid p_guid, uint64_t& r_stamp)
{
    const std::filesystem::path file = _asset_file(ctx, p_guid);
    const uint64_t asset_stamp = _stamp(file);
    const uint64_t clip_stamp = _stamp(_content_file(ctx, p_guid));
    if (asset_stamp == STAMP_MISSING || clip_stamp == STAMP_MISSING) return nullptr;
    r_stamp = _mix(asset_stamp, clip_stamp);
    ParsedAnimation& p = animations[p_guid];
    if (p.stamp != r_stamp) {
        p.stamp = r_stamp;
        p.mesh = {};
        if (p.clip.load_cooked(_content_file(ctx, p_guid)) != Error::OK) p.clip = LAnimation{};
        toml::table tbl;
        try { tbl = toml::parse_file(file.string()); }
        catch (const toml::parse_error&) {}
        p.mesh = Guid::from_string(tbl[asset_type_section(AssetType::ANIMATION)]["preview_mesh"].value_or(std::string()));
        if (!p.mesh.valid()) p.mesh = RigPreview::find_mesh(ctx, p.clip.skeleton);
    }
    return p.clip.frame_count && p.mesh.valid() ? &p : nullptr;
}

void ThumbnailCache::_pose_animation(Guid p_guid)
{
    auto it = animations.find(p_guid);
    const LSkeleton& skeleton = rig.skeleton_data();
    if (it == animations.end() || skeleton.joint_count() == 0) return;
    const LAnimation& clip = it->second.clip;
    std::vector<int32_t> map;
    clip_track_map(skeleton, clip, map);
    std::vector<JointTransform> local = skeleton.rest;
    clip_sample(clip, map, clip.duration * ANIMATION_POSE_FRACTION, local.data());
    rig.pose(local.data());
}

const LModel* ThumbnailCache::_parsed_model(EditorContext& ctx, Guid p_guid, uint64_t& r_stamp)
{
    const std::filesystem::path file = _asset_file(ctx, p_guid);
    r_stamp = _stamp(file);
    if (r_stamp == STAMP_MISSING) return nullptr;
    ParsedModel& p = models[p_guid];
    if (p.stamp != r_stamp) {
        p.stamp = r_stamp;
        if (p.model.load(file) != Error::OK) p.model = LModel{};
    }
    return p.model.guid == p_guid ? &p.model : nullptr;
}

bool ThumbnailCache::_resolve(EditorContext& ctx, Guid p_guid, AssetType p_type, Job& r_job)
{
    r_job = {};
    r_job.guid = p_guid;
    r_job.type = p_type;
    uint64_t key = _mix(_mix(_mix(0, VERSION), (uint64_t)p_type), p_guid.value);
    key = _mix(key, LMESH_VERSION);

    auto add_rig = [&](Guid p_mesh) {
        LMeshRig mesh_rig;
        if (!lmesh_read_rig(_content_file(ctx, p_mesh), mesh_rig) || !mesh_rig.skeleton.valid()) return;
        key = _mix(_mix(key, mesh_rig.skeleton.value), _stamp(_content_file(ctx, mesh_rig.skeleton)));
    };

    auto add_material = [&](Guid p_material) {
        uint64_t stamp = STAMP_MISSING;
        const LMaterial* m = _parsed_material(ctx, p_material, stamp);
        key = _mix(_mix(key, p_material.value), stamp);
        if (!m) return;
        std::vector<Guid> textures;
        _texture_refs(*m, textures);
        for (Guid t : textures) {
            key = _mix(_mix(key, t.value), _stamp(_content_file(ctx, t)));
            if (std::find(r_job.textures.begin(), r_job.textures.end(), t) == r_job.textures.end()) r_job.textures.push_back(t);
        }
    };

    switch (p_type) {
        case AssetType::MESH: {
            const uint64_t stamp = _stamp(_content_file(ctx, p_guid));
            if (stamp == STAMP_MISSING) return false;
            key = _mix(key, stamp);
            add_rig(p_guid);
            r_job.model = preview_mesh;
            r_job.mesh = p_guid;
        } break;
        case AssetType::MATERIAL: {
            uint64_t stamp = STAMP_MISSING;
            if (!_parsed_material(ctx, p_guid, stamp)) return false;
            key = _mix(_mix(key, BuiltinMeshes::SPHERE_REVISION), BuiltinMeshes::SPHERE.value);
            add_material(p_guid);
            r_job.model = preview_sphere;
            r_job.mesh = BuiltinMeshes::SPHERE;
            r_job.materials = { p_guid };
        } break;
        case AssetType::MODEL: {
            uint64_t stamp = STAMP_MISSING;
            const LModel* m = _parsed_model(ctx, p_guid, stamp);
            if (!m || !m->mesh.valid()) return false;
            key = _mix(_mix(_mix(key, stamp), m->mesh.value), _stamp(_content_file(ctx, m->mesh)));
            add_rig(m->mesh);
            r_job.model = p_guid;
            r_job.mesh = m->mesh;
            r_job.materials = m->materials;
            for (size_t i = 0; i < m->materials.size(); ++i) {
                key = _mix(key, i);
                if (m->materials[i].valid()) add_material(m->materials[i]);
            }
        } break;
        case AssetType::SKELETON: {
            const uint64_t stamp = _stamp(_content_file(ctx, p_guid));
            if (stamp == STAMP_MISSING) return false;
            r_job.model = preview_mesh;
            key = _mix(key, stamp);
        } break;
        case AssetType::ANIMATION: {
            uint64_t stamp = STAMP_MISSING;
            const ParsedAnimation* a = _parsed_animation(ctx, p_guid, stamp);
            if (!a) return false;
            const uint64_t mesh_stamp = _stamp(_content_file(ctx, a->mesh));
            if (mesh_stamp == STAMP_MISSING) return false;
            key = _mix(_mix(_mix(_mix(key, stamp), a->mesh.value), mesh_stamp), (uint64_t)(ANIMATION_POSE_FRACTION * 1000.0f));
            add_rig(a->mesh);
            r_job.model = preview_mesh;
            r_job.mesh = a->mesh;
        } break;
        default:
            return false;
    }
    r_job.key = key;
    return true;
}

bool ThumbnailCache::_skeleton_bones(EditorContext& ctx, Job& r_job, vec3& r_center, float& r_radius)
{
    LSkeleton skeleton;
    if (skeleton.load_cooked(_content_file(ctx, r_job.guid)) != Error::OK || skeleton.joint_count() == 0) return false;
    std::vector<mat4> global(skeleton.joint_count());
    skeleton.globals(skeleton.rest.data(), global.data());

    r_job.bones.resize(global.size());
    for (size_t i = 0; i < global.size(); ++i) r_job.bones[i] = vec3(global[i][3]);
    r_job.bone_parents = skeleton.parents;

    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (const vec3& p : r_job.bones) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    r_center = (lo + hi) * 0.5f;
    r_radius = std::max(length(hi - lo) * 0.5f, 0.01f);
    return true;
}

static void _plot(std::vector<uint8_t>& r_pixels, int p_x, int p_y, ImU32 p_color, float p_coverage)
{
    const int size = (int)ThumbnailCache::OUTPUT_SIZE;
    if (p_x < 0 || p_y < 0 || p_x >= size || p_y >= size || p_coverage <= 0.0f) return;
    uint8_t* px = r_pixels.data() + ((size_t)p_y * size + p_x) * 4;
    const float a = std::min(p_coverage, 1.0f);
    for (int c = 0; c < 3; c++) {
        const float src = (float)((p_color >> (8 * c)) & 0xFF);
        px[c] = (uint8_t)(px[c] + (src - px[c]) * a + 0.5f);
    }
}

static void _disk(std::vector<uint8_t>& r_pixels, vec2 p_center, float p_radius, ImU32 p_color)
{
    const int x0 = (int)std::floor(p_center.x - p_radius - 1.0f);
    const int y0 = (int)std::floor(p_center.y - p_radius - 1.0f);
    const int x1 = (int)std::ceil(p_center.x + p_radius + 1.0f);
    const int y1 = (int)std::ceil(p_center.y + p_radius + 1.0f);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) _plot(r_pixels, x, y, p_color, p_radius + 0.5f - length(vec2(x + 0.5f, y + 0.5f) - p_center));
    }
}

void ThumbnailCache::_draw_bones(const Job& p_job)
{
    const UiStyle& ui = ui_style();
    const float size = (float)OUTPUT_SIZE;
    const mat4 view_proj = p_job.camera.proj(1.0f) * p_job.camera.view();
    std::vector<vec2> screen(p_job.bones.size());
    std::vector<uint8_t> visible(p_job.bones.size(), 0);
    for (size_t i = 0; i < p_job.bones.size(); ++i) {
        const vec4 clip = view_proj * vec4(p_job.bones[i], 1.0f);
        if (clip.w <= p_job.camera.near_z) continue;
        screen[i] = vec2((clip.x / clip.w * 0.5f + 0.5f) * size, (0.5f - clip.y / clip.w * 0.5f) * size);
        visible[i] = 1;
    }

    const float line_radius = ui.bones.bone_thickness;
    for (size_t i = 0; i < screen.size(); ++i) {
        const int32_t parent = p_job.bone_parents[i];
        if (!visible[i] || parent == SKELETON_NO_JOINT || !visible[parent]) continue;
        const vec2 a = screen[parent];
        const vec2 b = screen[i];
        const int steps = std::max(1, (int)std::ceil(length(b - a) * 2.0f));
        for (int s = 0; s <= steps; s++) _disk(pixels, mix(a, b, (float)s / (float)steps), line_radius, ui.color.asset_skeleton);
    }
    for (size_t i = 0; i < screen.size(); ++i) {
        if (visible[i]) _disk(pixels, screen[i], ui.bones.joint_radius, ui.color.asset_skeleton);
    }
}

ThumbnailCache::Readiness ThumbnailCache::_ready(EditorContext& ctx, const Job& p_job)
{
    using enum Readiness;
    Renderer& r = *ctx.renderer;

    if (p_job.type == AssetType::SKELETON) return READY;
    if (p_job.mesh == BuiltinMeshes::SPHERE) {
        if (!BuiltinMeshes::ensure_sphere()) return FAIL;
        r.geometry.load(BuiltinMeshes::SPHERE, BuiltinMeshes::sphere_bin());
    } else if (!r.geometry.get(r.geometry.find(p_job.mesh))) {
        const std::filesystem::path bin = _content_file(ctx, p_job.mesh);
        if (_stamp(bin) == STAMP_MISSING) return FAIL;
        r.geometry.load(p_job.mesh, bin);
    }
    const uint32_t mesh_id = r.geometry.find(p_job.mesh);
    if (!r.geometry.get(mesh_id)) return FAIL;
    r.geometry.require(mesh_id);
    if (!r.geometry.settled(mesh_id)) return WAIT;

    for (Guid t : p_job.textures) {
        if (r.textures.get(t)) continue;
        const std::filesystem::path bin = _content_file(ctx, t);
        if (_stamp(bin) == STAMP_MISSING) continue;
        r.textures.load(t, bin);
        if (!r.textures.get(t)) return WAIT;
    }

    for (Guid g : p_job.materials) {
        if (!g.valid()) continue;
        auto parsed = materials.find(g);
        if (parsed == materials.end() || parsed->second.material.guid != g) continue;
        auto cached = r.materials.by_guid.find(g);
        if (cached == r.materials.by_guid.end()) {
            r.materials.set(parsed->second.material);
            return WAIT;
        }
        if (cached->second.material != parsed->second.material) return WAIT;
    }

    if (p_job.type == AssetType::MODEL) {
        auto parsed = models.find(p_job.model);
        if (parsed == models.end() || parsed->second.model.guid != p_job.model) return FAIL;
        const ModelCache::Entry* cached = r.models.get(p_job.model);
        if (!cached || cached->mesh != parsed->second.model.mesh || cached->materials != parsed->second.model.materials) {
            r.models.load(_asset_file(ctx, p_job.model));
            return WAIT;
        }
    }
    return READY;
}

/****************/
/**** UPDATE ****/
/****************/

VkImageView ThumbnailCache::get(EditorContext& ctx, Guid p_guid, AssetType p_type)
{
    if (!p_guid.valid() || !ctx.renderer) return VK_NULL_HANDLE;
    if (p_type == AssetType::TEXTURE) return _texture_view(ctx, p_guid);
    if (p_type != AssetType::MESH && p_type != AssetType::MATERIAL && p_type != AssetType::MODEL && p_type != AssetType::SKELETON && p_type != AssetType::ANIMATION) return VK_NULL_HANDLE;
    Entry& e = entries[p_guid];
    if (e.type != p_type) {
        e.type = p_type;
        e.key = e.shown_key = e.failed_key = 0;
        e.next_check = 0.0;
    }
    e.seen_frame = ctx.renderer->frame_number;
    return e.image.image_view;
}

void ThumbnailCache::update(EditorContext& ctx)
{
    if (!ctx.renderer || !ctx.project || !ctx.imgui || !ctx.renderer->frame_acquired) return;
    Renderer& r = *ctx.renderer;
    if (!disk_scanned) _scan_disk();
    _collect(ctx, false);

    if (path) {
        uint64_t ticket = 0;
        if (path->capture.collect(r.current_frame, ticket, pixels) && job.state == JobState::RENDERING && ticket == job.ticket) _finish(ctx);
    }
    if (job.state == JobState::RENDERING && r.frame_number > job.frame + r.frame_count + RENDER_TIMEOUT_FRAMES) job = {};
    if (job.state == JobState::PREPARED && r.frame_number > job.frame) _render(ctx);

    _check_visible(ctx);
    if (job.state == JobState::NONE) _start_next(ctx);
}

void ThumbnailCache::_check_visible(EditorContext& ctx)
{
    const uint64_t frame = ctx.renderer->frame_number;
    const double now = ImGui::GetTime();
    for (auto& [guid, e] : entries) {
        if (e.seen_frame + VISIBLE_FRAMES < frame || now < e.next_check) continue;
        e.next_check = now + CHECK_INTERVAL_S;
        Job resolved;
        if (!_resolve(ctx, guid, e.type, resolved)) continue;
        e.key = resolved.key;
        if (resolved.key == e.shown_key || resolved.key == e.failed_key || e.queued) continue;
        if (auto it = disk.find(guid); it != disk.end() && it->second == resolved.key && _load_disk(ctx, guid, e, resolved.key)) continue;
        if (job.state != JobState::NONE && job.guid == guid && job.key == resolved.key) continue;
        e.queued = true;
        queue.push_back(guid);
    }
}

void ThumbnailCache::_start_next(EditorContext& ctx)
{
    Renderer& r = *ctx.renderer;
    for (uint32_t attempt = 0; attempt < START_ATTEMPTS && !queue.empty(); ++attempt) {
        const Guid guid = queue.front();
        queue.pop_front();
        auto it = entries.find(guid);
        if (it == entries.end()) continue;
        Entry& e = it->second;
        e.queued = false;
        if (e.seen_frame + VISIBLE_FRAMES < r.frame_number) continue;

        Job next;
        if (!_resolve(ctx, guid, e.type, next)) continue;
        e.key = next.key;
        if (next.key == e.shown_key) continue;

        const Readiness ready = _ready(ctx, next);
        if (ready == Readiness::FAIL) {
            e.failed_key = next.key;
            continue;
        }
        if (ready == Readiness::WAIT) {
            e.queued = true;
            queue.push_back(guid);
            continue;
        }
        if (_ensure_view(ctx) != Error::OK) return;

        rig.invalidate();
        rig.refresh(ctx, next.mesh, false);
        if (next.type == AssetType::ANIMATION) _pose_animation(next.guid);
        rig.apply(world, entity);
        vec3 center(0.0f);
        float bounds_radius = 0.0f;
        const bool has_mesh = rig.bounds(r.geometry, next.mesh, center, bounds_radius);
        if (next.type == AssetType::SKELETON) {
            if (!_skeleton_bones(ctx, next, center, bounds_radius)) continue;
        } else if (!has_mesh) {
            continue;
        }
        const float fov = glm::radians(CAMERA_FOV_DEG);
        const float radius = std::max(bounds_radius, 0.001f);
        const float distance = radius / std::sin(fov * 0.5f) * FRAME_MARGIN;
        const float yaw = glm::radians(CAMERA_YAW_DEG);
        const float pitch = glm::radians(CAMERA_PITCH_DEG);
        const glm::vec3 offset(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        next.camera.fov_y = fov;
        next.camera.position = center + offset * distance;
        next.camera.rotation = glm::quatLookAt(-offset, glm::vec3(0.0f, 1.0f, 0.0f));
        next.camera.near_z = std::max(distance - radius * 1.5f, distance * 0.01f);
        next.camera.far_z = distance + radius * 2.0f;

        if (next.type != AssetType::MODEL) r.models.set(next.model, next.mesh, next.materials);
        if (ModelComponent* mc = world.try_get<ModelComponent>(entity); mc && mc->model != next.model) {
            mc->model = next.model;
            world.touch(entity);
        }
        next.state = JobState::PREPARED;
        next.frame = r.frame_number;
        job = std::move(next);
        return;
    }
}

void ThumbnailCache::_render(EditorContext& ctx)
{
    Renderer& r = *ctx.renderer;
    render_view.camera = job.camera;
    render_view.camera_cut_pending = true;
    job.ticket = ++ticket_serial;
    path->capture.request = job.ticket;
    if (r.view_render(render_view, world) != Error::OK) {
        path->capture.request = 0;
        job = {};
        return;
    }
    job.state = JobState::RENDERING;
    job.frame = r.frame_number;
}

void ThumbnailCache::_finish(EditorContext& ctx)
{
    const Job done = std::move(job);
    job = {};
    auto it = entries.find(done.guid);
    if (it == entries.end()) return;
    Entry& e = it->second;
    if (!done.bones.empty()) _draw_bones(done);

    _swap_image(ctx, e, dd->image_create_texture(pixels.data(), OUTPUT_SIZE, OUTPUT_SIZE, "thumbnail"), done.key);
    if (dir.empty()) return;

    std::filesystem::path old;
    if (auto d = disk.find(done.guid); d != disk.end() && d->second != done.key) old = _disk_path(done.guid, d->second);
    disk[done.guid] = done.key;
    PendingWrite* write = writes.emplace_back(std::make_unique<PendingWrite>()).get();
    job_system().run_background_owned([write, file = _disk_path(done.guid, done.key), old, data = pixels]() mutable {
        ImageData<uint8_t, 4> img{};
        img.pixels = data.data();
        img.width = (int)OUTPUT_SIZE;
        img.height = (int)OUTPUT_SIZE;
        const bool ok = ImageIO::save_png(file.wstring(), img);
        std::error_code ec;
        if (ok && !old.empty()) std::filesystem::remove(old, ec);
        write->ok = ok;
    }, &write->counter, "Thumbnail Write");
}

}
