#pragma once
#include <editor/assets/texture_viewer.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <string>
#include <vector>

namespace lumen {

struct LTexture;

struct TextureDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::TEXTURE;
    static constexpr const char* ICON = ICON_FA_IMAGE;
    static constexpr float MIN_ZOOM = 1.0f / 64.0f;
    static constexpr float MAX_ZOOM = 64.0f;
    static constexpr float ZOOM_STEP = 1.2f;
    static constexpr float CHECKER_SIZE = 12.0f;

    struct View { VkImageView view = VK_NULL_HANDLE; uint8_t mask = 0; };

    uint8_t channels = TextureViewer::CHANNEL_ALL;
    bool normal_view = false;
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    std::string usage;

    bool _fit_pending = true;
    VkImage _image = VK_NULL_HANDLE;
    std::vector<View> _views;

    const char* _type_name() const override { return "Texture"; }
    const Scene* preview_scene() const override { return nullptr; }
    const Camera* preview_camera() const override { return nullptr; }
    const LTexture* texture(EditorContext& ctx) const;
    uint8_t visible_channels(EditorContext& ctx) const;
    void _read_info();
    void _draw_canvas(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_toolbar(EditorContext& ctx);
    VkImageView _view(EditorContext& ctx, const drivers::DeviceDriverVulkan::Image& p_image);
    void _release_views(EditorContext& ctx);

    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
