#pragma once
#include <core/assets/llocomotion.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct LocomotionDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::LOCOMOTION;
    static constexpr const char* ICON = ICON_FA_PERSON_WALKING;

    LLocomotionSet set;
    LLocomotionSet saved;

    const char* _type_name() const override { return "Locomotion Set"; }

    uint32_t auto_fill(EditorContext& ctx);

    bool dirty() const override { return set != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
