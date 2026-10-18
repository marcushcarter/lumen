#pragma once
#include <core/base/error.h>
#include <core/assets/guid.h>
#include <editor/editor_context.h>
#include <cstdint>

namespace lumen {

struct Camera;

enum class TabKind : uint8_t { WORLD, EDITOR_SETTINGS, MODEL, MATERIAL };

struct EditorTab
{
    TabKind kind = TabKind::WORLD;
    Guid guid;

    bool _owned = false;
    bool _created = false;
    uint64_t _last_selected = 0;

    virtual ~EditorTab() = default;

    virtual const char* title() const = 0;
    virtual bool closable() const { return true; }
    virtual bool dirty() const { return false; }
    virtual const Camera* camera() const { return nullptr; }

    virtual Error create(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void destroy(EditorContext& ctx) { (void)ctx; }
    virtual Error save(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void on_tick(EditorContext& ctx) { (void)ctx; }
    virtual void on_update(EditorContext& ctx, float p_dt) = 0;
};

}