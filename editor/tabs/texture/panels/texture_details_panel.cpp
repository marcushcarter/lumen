#include <editor/tabs/texture/panels/texture_details_panel.h>
#include <editor/tabs/texture/texture_tab.h>
#include <core/base/strings.h>
#include <core/rendering/renderer.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void TextureDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;

    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(tab->guid) : nullptr;
    ImGui::Text("Name: %s", e ? e->name.c_str() : "None");
    ImGui::Text("Path: %s", e ? e->asset_path.c_str() : "-");
    if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
    if (!tab->source.empty()) {
        ImGui::TextUnformatted("Source:");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tab->source.c_str());
        ImGui::PopTextWrapPos();
    }

    const LTexture* tex = tab->texture(ctx);
    if (!tex) {
        ImGui::TextDisabled("Texture is not loaded.");
        return;
    }

    const drivers::DeviceDriverVulkan::Image& image = tex->image;
    char format[64];
    TextureViewer::format_name(image.format, format, sizeof(format));

    ImGui::Text("Memory: %s", fmt_bytes(image.mem_req.size));
    ImGui::Text("Resolution: %u x %u", image.extent.width, image.extent.height);
    ImGui::Text("Format: %s", format);
    ImGui::Text("Channels: %u", TextureViewer::channel_count(image.format));
    ImGui::Text("Mips: %u", image.mip_levels);
    if (!tab->usage.empty()) ImGui::Text("Usage: %s", tab->usage.c_str());
}

}
