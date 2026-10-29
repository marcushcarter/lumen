#include <core/rendering/features/editor/grid_feature.h>
#include <core/rendering/features/editor/editor_overlay.h>
#include <core/rendering/frame_data.h>
#include <core/rendering/world_gpu.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lumen {

static glm::vec4 _row(const glm::mat4& p_m, int p_row)
{
    return glm::vec4(p_m[0][p_row], p_m[1][p_row], p_m[2][p_row], p_m[3][p_row]);
}

void GridFeature::_write_params(const CameraUniform& p_camera, VkExtent2D p_size, VkExtent2D p_depth_size, Params& r_params) const
{
    const glm::vec2 size((float)p_size.width, (float)p_size.height);
    const glm::vec3 cam = glm::vec3(p_camera.position);

    glm::mat4 unjitter(1.0f);
    unjitter[3][0] = -p_camera.jitter.x;
    unjitter[3][1] = -p_camera.jitter.y;
    const glm::mat4 view_proj = unjitter * p_camera.curr_view_proj;
    const glm::mat4 inv = glm::inverse(view_proj);

    auto column = [&](int p_i) { return glm::vec3(inv[p_i]) - cam * inv[p_i].w; };
    glm::vec3 ray = -column(0) - column(1) + column(2) + column(3);
    glm::vec3 ray_dx = column(0) * (2.0f / size.x);
    glm::vec3 ray_dy = column(1) * (2.0f / size.y);
    const glm::vec4 center = inv * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const glm::vec3 center_ray = ray + ray_dx * size.x * 0.5f + ray_dy * size.y * 0.5f;
    if (glm::dot(center_ray, glm::vec3(center) / center.w - cam) < 0.0f) {
        ray = -ray;
        ray_dx = -ray_dx;
        ray_dy = -ray_dy;
    }

    r_params.clip_z = _row(view_proj, 2);
    r_params.clip_w = _row(view_proj, 3);
    r_params.camera = glm::vec4(cam, std::max(FADE_MIN, std::abs(cam.y) * FADE_HEIGHT));
    r_params.ray = glm::vec4(ray, 0.0f);
    r_params.ray_dx = glm::vec4(ray_dx, 0.0f);
    r_params.ray_dy = glm::vec4(ray_dy, 0.0f);

    const float scale = ui_width > 0.0f ? size.x / ui_width : 1.0f;
    const float thickness = axis_thickness * scale;
    const float length = head_length * scale;
    const float width = head_width * scale;
    r_params.axis_style = glm::vec4(thickness, length, width, 0.0f);

    auto project = [&](glm::vec3 p_world, glm::vec4& r_out) {
        const glm::vec4 clip = view_proj * glm::vec4(p_world, 1.0f);
        if (clip.w <= p_camera.near_z) {
            r_out = glm::vec4(0.0f);
            return false;
        }
        const glm::vec2 pixel = (glm::vec2(clip) / clip.w * 0.5f + 0.5f) * size;
        r_out = glm::vec4(pixel, clip.z / clip.w, 1.0f);
        return true;
    };

    glm::vec2 lo(1.0f);
    glm::vec2 hi(0.0f);
    const bool origin = project(glm::vec3(0.0f), r_params.axis_origin);
    for (int i = 0; i < 3; i++) {
        r_params.axis_color[i] = axis_color[i];
        glm::vec3 axis(0.0f);
        axis[i] = 1.0f;
        glm::vec4& tip = r_params.axis_tip[i];
        if (!origin || !project(axis, tip) || glm::distance(glm::vec2(tip), glm::vec2(r_params.axis_origin)) < 1.0f) {
            tip = glm::vec4(0.0f);
            continue;
        }
        if (hi.x < lo.x) {
            lo = glm::vec2(r_params.axis_origin);
            hi = lo;
        }
        lo = glm::min(lo, glm::vec2(tip));
        hi = glm::max(hi, glm::vec2(tip));
    }
    const float pad = std::max(thickness, width) + AXIS_BOUNDS_PAD;
    r_params.axis_bounds = hi.x < lo.x ? glm::vec4(1.0f, 1.0f, 0.0f, 0.0f) : glm::vec4(lo - pad, hi + pad);
    r_params.size = glm::uvec4(p_size.width, p_size.height, p_depth_size.width, p_depth_size.height);
}

void GridFeature::_create_grid_pass()
{
    grid_pass.name = "EditorGrid";
    grid_pass.category = PASS_CATEGORY_EDITOR;
    grid_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image(editor_overlay_target(*b.graph), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    grid_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto* depth = cl.graph->image("G_Depth");
        auto* out = cl.graph->image(editor_overlay_target(*cl.graph));
        drivers::DeviceDriverVulkan::Buffer& buf = param_buffers[cl.graph->current_frame];
        if (!depth || !out || !buf.buffer) return;

        Params params{};
        _write_params(ctx->frame->camera, out->extent, depth->extent, params);
        std::memcpy(buf.mapped, &params, sizeof(params));
        ctx->dd->buffer_flush(buf);

        struct Push {
            VkDeviceAddress params_addr;
            uint32_t depth_index;
            uint32_t out_slot;
            uint32_t flags;
        } pc{};
        pc.params_addr = buf.device_address;
        pc.depth_index = depth->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.flags = (show_grid ? 1u : 0u) | (show_axes ? 2u : 0u);

        cl.dd->command_bind_pipeline(cl.cmd, grid_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Editor grid", (out->extent.width + 7) / 8, (out->extent.height + 7) / 8);
    };
}

Error GridFeature::create_resources()
{
    param_buffers.resize(ctx->graph->frame_count);
    for (drivers::DeviceDriverVulkan::Buffer& b : param_buffers) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.size = sizeof(Params);
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.device_local = false;
        ci.host_visible = true;
        ci.name = "EditorGridParams";
        b = ctx->dd->buffer_create(ci);
        LUMEN_ERR_FAIL_COND_V(!b.buffer, Error::FAILED);
    }
    _create_grid_pass();
    return Error::OK;
}

Error GridFeature::create_pipelines()
{
    grid_pipe = _compute_pipeline(L"SHADERS_EDITOR_GRID_COMP", "editor/grid.comp");
    return Error::OK;
}

void GridFeature::destroy_resources()
{
    ctx->dd->pipeline_free(grid_pipe);
    for (drivers::DeviceDriverVulkan::Buffer& b : param_buffers) {
        if (b.buffer) ctx->dd->buffer_free(b);
    }
    param_buffers.clear();
}

void GridFeature::build(RenderGraph& g)
{
    if (!show_grid && !show_axes) return;
    if (param_buffers.empty() || !editor_overlay_target(g) || !g.image_resource("G_Depth")) return;
    g.add(&grid_pass);
}

}
