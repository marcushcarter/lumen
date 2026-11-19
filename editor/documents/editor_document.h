#pragma once
#include <core/base/error.h>
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <imgui.h>
#include <cstdint>
#include <filesystem>

namespace lumen {

struct Scene;
struct Camera;
struct DocumentLayout;

enum class DocumentKind : uint8_t { EDITOR_SETTINGS, PROJECT_SETTINGS, MODEL, MATERIAL, TEXTURE, MESH, ANIMATION, TERRAIN, VERSION_CONTROL, GPU_PROFILER, MEMORY };

struct EditorDocument
{
    DocumentKind kind = DocumentKind::MODEL;
    Guid guid;

    bool _created = false;
    bool _focus_request = true;

    virtual ~EditorDocument() = default;

    virtual const char* title() const = 0;
    virtual const char* tooltip() const { return nullptr; }
    virtual bool dirty() const { return false; }
    virtual bool locked(const EditorContext& ctx) const { (void)ctx; return false; }
    virtual const std::filesystem::path* asset_path() const { return nullptr; }
    virtual const Scene* preview_scene() const { return nullptr; }
    virtual const Camera* preview_camera() const { return nullptr; }
    virtual DocumentLayout* panels() { return nullptr; }

    virtual Error create(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void destroy(EditorContext& ctx) { (void)ctx; }
    virtual Error save(EditorContext& ctx) { (void)ctx; return Error::OK; }
    virtual void on_tick(EditorContext& ctx) { (void)ctx; }
    virtual void on_update(EditorContext& ctx, float p_dt) = 0;
};

}
