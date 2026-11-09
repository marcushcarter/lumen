#include <editor/tabs/texture/panels/texture_details_panel.h>
#include <editor/tabs/texture/texture_tab.h>
#include <core/base/strings.h>
#include <core/rendering/renderer.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void TextureDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    if (tab->texture(ctx)) return;
    ui_warning("Texture is not loaded");
}

void TextureDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    ui_field_push_style();

    tab->draw_import();

    if (ui_category("Info")) {
        if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
        if (const LTexture* tex = tab->texture(ctx)) {
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

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
