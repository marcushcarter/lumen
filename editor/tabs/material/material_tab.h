#pragma once
#include <core/assets/lmaterial.h>
#include <editor/tabs/asset_tab.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MaterialTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::MATERIAL;
    static constexpr const char* ICON = ICON_FA_PALETTE;

    LMaterial material;
    LMaterial saved;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    void _frame_preview(EditorContext& ctx);

    const char* title() const override { return _title.empty() ? "Material" : _title.c_str(); }
    bool dirty() const override { return material != saved; }
    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
