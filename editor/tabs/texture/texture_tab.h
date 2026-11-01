#pragma once
#include <editor/assets/texture_viewer.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <string>
#include <vector>

namespace lumen {

struct LTexture;

struct TextureTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::TEXTURE;
    static constexpr const char* ICON = ICON_FA_IMAGE;
    static constexpr float MIN_ZOOM = 1.0f / 64.0f;
    static constexpr float MAX_ZOOM = 64.0f;
    static constexpr float ZOOM_STEP = 1.2f;
    static constexpr float CHECKER_SIZE = 12.0f;
    static constexpr float MIP_COMBO_WIDTH = 150.0f;

    struct View { VkImageView view = VK_NULL_HANDLE; int32_t mip = -1; uint8_t mask = 0; };

    uint8_t channels = TextureViewer::CHANNEL_ALL;
    bool normal_view = false;
    int32_t mip = -1;
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    std::string usage;
    std::string source;

    bool _fit_pending = true;
    VkImage _image = VK_NULL_HANDLE;
    std::vector<View> _views;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_texture; }
    const LTexture* texture(EditorContext& ctx) const;
    uint8_t visible_channels(EditorContext& ctx) const;
    void _read_info();
    void _draw_canvas(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void _draw_toolbar(EditorContext& ctx) override;
    VkImageView _view(EditorContext& ctx, const drivers::DeviceDriverVulkan::Image& p_image);
    void _release_views(EditorContext& ctx);

    const char* title() const override { return _title.empty() ? "Texture" : _title.c_str(); }
    const Camera* camera() const override { return nullptr; }
    const World* scene() const override { return nullptr; }
    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
