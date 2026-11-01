#include <editor/tabs/texture/panels/texture_details_panel.h>
#include <editor/tabs/texture/texture_tab.h>
#include <core/rendering/renderer.h>
#include <imgui.h>

namespace lumen {

void TextureDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    const LTexture* tex = tab->texture(ctx);
    if (!tex) {
        ImGui::TextDisabled("Texture is not loaded.");
        return;
    }

    const drivers::DeviceDriverVulkan::Image& image = tex->image;
    char format[64];
    TextureViewer::format_name(image.format, format, sizeof(format));

    ImGui::Text("Resolution: %u x %u", image.extent.width, image.extent.height);
    ImGui::Text("Format: %s", format);
    ImGui::Text("Channels: %u", TextureViewer::channel_count(image.format));
    ImGui::Text("Mips: %u", image.mip_levels);
    ImGui::Text("Memory: %.2f MB", (double)image.mem_req.size / (1024.0 * 1024.0));
    if (!tab->usage.empty()) ImGui::Text("Usage: %s", tab->usage.c_str());
    if (!tab->source.empty()) {
        ImGui::TextUnformatted("Source:");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tab->source.c_str());
        ImGui::PopTextWrapPos();
    }
}

}
