#include <core/rendering/features/screenshot_feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/io/image_io.h>
#include <core/io/path.h>
#include <vector>
#include <chrono>
#include <ctime>

namespace lumen {

Error ScreenshotFeature::create_resources()
{
    screenshot_pass.name = "Screenshot";
    screenshot_pass.category = "Present";
    screenshot_pass.never_cull = true;
    screenshot_pass.setup = [](RenderGraph::Builder& b) {
        b.read_image("Backbuffer", VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    };
    screenshot_pass.execute = [this](RenderGraph::CommandList& cl) {
        auto bb = cl.graph->image("Backbuffer");
        cl.dd->command_copy_image_to_buffer(cl.cmd, *bb, staging, bb->extent);
    };

    return Error::Ok;
};

void ScreenshotFeature::destroy_resources()
{
    if (encode_job.valid()) encode_job.wait();
    if (staging.buffer) ctx->dd->buffer_free(staging);
}

void ScreenshotFeature::_writeback()
{
    ctx->dd->device_wait_idle();
    
    if (!staging.mapped) { log_write("Screenshot: staging buffer not mapped."); return; }

    const uint8_t* src = static_cast<const uint8_t*>(staging.mapped);
    const size_t px = (size_t)capture_width * capture_height;

    const bool bgra = (capture_format == VK_FORMAT_B8G8R8A8_SRGB || capture_format == VK_FORMAT_B8G8R8A8_UNORM);

    std::vector<uint8_t> rgba(px * 4);
    for (size_t i = 0; i < px; ++i) {
        rgba[i*4+0] = src[i*4 + (bgra ? 2 : 0)];
        rgba[i*4+1] = src[i*4 + 1];
        rgba[i*4+2] = src[i*4 + (bgra ? 0 : 2)];
        rgba[i*4+3] = 255;
    }

    char name[64];
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    std::strftime(name, sizeof(name), "screenshot_%Y%m%d_%H%M%S.png", &lt);

    encode_path = Paths::screenshots() / name;
    encode_job = std::async(std::launch::async, [path = encode_path, pixels = std::move(rgba), w = (int)capture_width, h = (int)capture_height]() mutable -> bool {
        ImageData<uint8_t, 4> img{};
        img.pixels = pixels.data();
        img.width = w;
        img.height = h;
        return ImageIO::save_png(path.wstring(), img);
    });
}

void ScreenshotFeature::build(RenderGraph& g)
{
    if (!enabled) return;

    if (encode_job.valid() && encode_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (encode_job.get()) {
            Paths::reveal_in_explorer(encode_path);
        } else {
            log_write("Screenshot: failed to write %s", encode_path.string().c_str());
        }
    }

    if (pending) {
        _writeback();
        pending = false;
    }

    if (!requested) return;
    requested = false;

    const drivers::DeviceDriverVulkan::Swapchain& sc = ctx->dd->swapchain;
    if (sc.images.empty()) return;

    capture_width = sc.images[0].extent.width;
    capture_height = sc.images[0].extent.height;
    capture_format = sc.format;

    const VkDeviceSize bytes = (VkDeviceSize)capture_width * capture_height * 4;

    if (!staging.buffer) {
        drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
        ci.name = "Screenshot_Staging";
        ci.size = bytes;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ci.host_visible = true;
        staging = ctx->dd->buffer_create(ci);
        if (!staging.buffer) { log_write("Screenshot: staging alloc failed."); return; }
    } else if (ctx->dd->buffer_ensure_capacity(staging, bytes) != Error::Ok) {
        return;
    }

    g.add(&screenshot_pass);
    pending = true;
};

}