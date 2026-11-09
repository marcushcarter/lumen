#pragma once
#include <core/base/error.h>
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <imgui.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

struct Camera;
struct World;

enum class TabKind : uint8_t { WORLD, EDITOR_SETTINGS, MODEL, MATERIAL, TEXTURE, MESH, PROJECT_SETTINGS, ANIMATION, BLEND_SPACE, OVERLAY, TERRAIN, ASSET_MANAGER };

struct EditorTab
{
    TabKind kind = TabKind::WORLD;
    Guid guid;

    bool _owned = false;
    bool _created = false;
    uint64_t _last_selected = 0;

    virtual ~EditorTab() = default;

    virtual const char* title() const = 0;
    virtual ImU32 icon_color() const { return 0; }
    virtual const char* tooltip() const { return nullptr; }
    virtual bool closable() const { return true; }
    virtual bool dirty() const { return false; }
    virtual const std::filesystem::path* asset_path() const { return nullptr; }
    virtual const Camera* camera() const { return nullptr; }
    virtual const World* scene() const { return nullptr; }
    virtual bool shows_viewport() const { return false; }

    virtual Error create(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void destroy(EditorContext& ctx) { (void)ctx; }
    virtual Error save(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void on_tick(EditorContext& ctx) { (void)ctx; }
    virtual void on_update(EditorContext& ctx, float p_dt) = 0;
};

}
