#include <editor/tabs/texture/texture_tab.h>
#include <core/assets/asset_common.h>
#include <core/assets/ltexture.h>
#include <core/rendering/renderer.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/tabs/texture/panels/texture_details_panel.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/ui/ui_style.h>
#include <toml++/toml.hpp>
#include <imgui.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

namespace lumen {

using Image = drivers::DeviceDriverVulkan::Image;

static void _draw_checker(ImDrawList* p_dl, ImVec2 p_min, ImVec2 p_max, ImVec2 p_clip_min, ImVec2 p_clip_max)
{
    const ImVec2 v0(std::max(p_min.x, p_clip_min.x), std::max(p_min.y, p_clip_min.y));
    const ImVec2 v1(std::min(p_max.x, p_clip_max.x), std::min(p_max.y, p_clip_max.y));
    if (v0.x >= v1.x || v0.y >= v1.y) return;

    const float s = TextureTab::CHECKER_SIZE;
    p_dl->AddRectFilled(v0, v1, IM_COL32(58, 58, 58, 255));
    const int x0 = (int)std::floor((v0.x - p_min.x) / s);
    const int y0 = (int)std::floor((v0.y - p_min.y) / s);
    const int x1 = (int)std::ceil((v1.x - p_min.x) / s);
    const int y1 = (int)std::ceil((v1.y - p_min.y) / s);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0 + ((x0 + y) & 1 ? 0 : 1); x < x1; x += 2) {
            const ImVec2 a(std::max(p_min.x + x * s, v0.x), std::max(p_min.y + y * s, v0.y));
            const ImVec2 b(std::min(p_min.x + (x + 1) * s, v1.x), std::min(p_min.y + (y + 1) * s, v1.y));
            p_dl->AddRectFilled(a, b, IM_COL32(84, 84, 84, 255));
        }
    }
}

Error TextureTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();
    _read_info();
    normal_view = usage == texture_usage_name(TextureUsage::NORMAL);

    _create_layout(ctx, "TextureHost");
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { _draw_canvas(r_ctx, p_min, p_max); };
    layout.add_panel<TextureDetailsPanel>()->tab = this;
    return OK;
}

void TextureTab::destroy(EditorContext& ctx)
{
    _release_views(ctx);
    layout.shutdown();
}

void TextureTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    const LTexture* tex = texture(ctx);
    const VkImage image = tex ? tex->image.image : VK_NULL_HANDLE;
    if (image != _image) {
        _release_views(ctx);
        _image = image;
    }
    _draw_layout(ctx);
}

const LTexture* TextureTab::texture(EditorContext& ctx) const
{
    return ctx.renderer ? ctx.renderer->textures.get(guid) : nullptr;
}

uint8_t TextureTab::visible_channels(EditorContext& ctx) const
{
    const LTexture* tex = texture(ctx);
    if (!tex) return 0;
    const uint8_t mask = channels & TextureViewer::channel_mask(TextureViewer::channel_count(tex->image.format));
    return normal_view ? mask | TextureViewer::VIEW_NORMAL : mask;
}

void TextureTab::_read_info()
{
    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) { return; }
    usage = tbl[asset_type_section(AssetType::TEXTURE)]["usage"].value_or(std::string());
}

void TextureTab::_draw_toolbar(EditorContext& ctx)
{
    static constexpr const char* LABELS[4] = { "R##channel_r", "G##channel_g", "B##channel_b", "A##channel_a" };
    static constexpr const char* TIPS[4] = { "Red", "Green", "Blue", "Alpha" };
    const LTexture* tex = texture(ctx);
    const uint8_t available = tex ? TextureViewer::channel_mask(TextureViewer::channel_count(tex->image.format)) : 0;

    TabToolbar bar;
    bar.begin();
    bar.save(ctx, *this);
    _draw_reimport_buttons(ctx, bar);
    bar.gap();

    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t bit = (uint8_t)(1u << i);
        const bool enabled = (available & bit) != 0;
        if (bar.button(LABELS[i], enabled ? TIPS[i] : "Not in this texture", enabled && (channels & bit), enabled)) channels ^= bit;
    }
    bar.gap();

    char percent[16];
    std::snprintf(percent, sizeof(percent), "%.0f%%", zoom * 100.0f);
    bar.text(percent);
    bar.end();
}

void TextureTab::_draw_canvas(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);
    if (size.x < 1.0f || size.y < 1.0f) return;

    dl->AddRectFilled(p_min, p_max, ui.color.sunken);
    const LTexture* tex = texture(ctx);
    if (!tex) {
        const char* msg = "Texture is not loaded.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(ImFloor(p_min.x + (size.x - ts.x) * 0.5f), ImFloor(p_min.y + (size.y - ts.y) * 0.5f)), ui.color.text_dim, msg);
        return;
    }

    const Image& image = tex->image;
    const float w = (float)image.extent.width;
    const float h = (float)image.extent.height;
    if (w < 1.0f || h < 1.0f) return;
    const ImVec2 center(p_min.x + size.x * 0.5f, p_min.y + size.y * 0.5f);

    ImGui::SetCursorScreenPos(p_min);
    ImGui::InvisibleButton("##texture_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) {
        pan.x += io.MouseDelta.x;
        pan.y += io.MouseDelta.y;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) _fit_pending = true;

    if (_fit_pending) {
        zoom = std::clamp(std::min(size.x / w, size.y / h), MIN_ZOOM, MAX_ZOOM);
        pan = ImVec2(0.0f, 0.0f);
        _fit_pending = false;
    }

    if (hovered && io.MouseWheel != 0.0f) {
        const float next = std::clamp(zoom * std::pow(ZOOM_STEP, io.MouseWheel), MIN_ZOOM, MAX_ZOOM);
        const ImVec2 rel((io.MousePos.x - center.x - pan.x) / zoom, (io.MousePos.y - center.y - pan.y) / zoom);
        pan.x = io.MousePos.x - center.x - rel.x * next;
        pan.y = io.MousePos.y - center.y - rel.y * next;
        zoom = next;
    }

    const ImVec2 p0(std::floor(center.x + pan.x - w * zoom * 0.5f), std::floor(center.y + pan.y - h * zoom * 0.5f));
    const ImVec2 p1(p0.x + w * zoom, p0.y + h * zoom);

    dl->PushClipRect(p_min, p_max, true);
    const uint8_t visible = visible_channels(ctx);
    if ((visible & TextureViewer::CHANNEL_A) && std::popcount((uint8_t)(visible & TextureViewer::CHANNEL_ALL)) > 1) _draw_checker(dl, p0, p1, p_min, p_max);
    const TextureViewer& viewer = *ctx.texture_viewer;
    const VkSampler sampler = zoom >= 1.0f ? viewer.sampler_nearest.sampler : viewer.sampler_linear.sampler;
    const VkDescriptorSet set = ctx.imgui->texture_cache.get(_view(ctx, image), sampler);
    if (set) dl->AddImage((ImTextureID)set, p0, p1);
    dl->AddRect(ImVec2(p0.x - 1.0f, p0.y - 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f), ui.color.line);
    dl->PopClipRect();
}

VkImageView TextureTab::_view(EditorContext& ctx, const Image& p_image)
{
    const uint8_t mask = visible_channels(ctx);
    for (const View& v : _views) {
        if (v.mask == mask) return v.view;
    }
    const VkImageView view = ctx.texture_viewer->create_view(p_image, -1, mask);
    if (view) _views.push_back({ view, mask });
    return view;
}

void TextureTab::_release_views(EditorContext& ctx)
{
    for (const View& v : _views) ctx.texture_viewer->retire(ctx, v.view);
    _views.clear();
    _image = VK_NULL_HANDLE;
}

}
