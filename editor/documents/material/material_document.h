#pragma once
#include <core/assets/lmaterial.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MaterialDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::MATERIAL;
    static constexpr const char* ICON = ICON_FA_PALETTE;

    LMaterial material;
    LMaterial saved;
    bool _framed = false;

    const char* _type_name() const override { return "Material"; }
    void _frame_preview(EditorContext& ctx);

    bool dirty() const override { return material != saved; }
    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
