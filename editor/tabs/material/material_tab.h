#pragma once
#include <editor/tabs/editor_tab.h>
#include <core/assets/lmaterial.h>
#include <editor/layout/editor_layout.h>
#include <editor/viewport/orbit_camera.h>
#include <core/world/world.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <string>

namespace lumen {

struct MaterialTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::MATERIAL;
    static constexpr const char* ICON = ICON_FA_PALETTE;

    LMaterial material;
    LMaterial saved;
    std::filesystem::path path;
    EditorLayout layout;
    OrbitCamera orbit;
    World preview;
    Guid _preview_model;
    bool _framed = false;
    std::string _title;

    const char* title() const override { return _title.empty() ? "Material" : _title.c_str(); }
    bool dirty() const override { return !(material == saved); }
    const Camera* camera() const override { return &orbit.camera; }
    const World* scene() const override { return &preview; }

    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _frame_preview(EditorContext& ctx);
    void _refresh_title();
};

}