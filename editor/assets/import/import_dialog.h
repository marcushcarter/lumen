#pragma once
#include <editor/assets/import/model_cooker.h>
#include <editor/assets/import/texture_cooker.h>
#include <editor/assets/import/reimport.h>
#include <filesystem>
#include <deque>

namespace lumen {

struct EditorContext;

enum class ImportKind : uint8_t { NONE, TEXTURE, MODEL, MESH, COLLISION };

ImportKind import_kind(const std::filesystem::path& p_src);
void collision_settings_fields(CollisionCooker::Settings& r_settings);

struct ImportDialog
{
    static constexpr size_t NAME_SIZE = 128;

    struct Pending {
        std::filesystem::path source;
        std::filesystem::path dst_dir;
        ImportKind kind = ImportKind::NONE;
        char name[NAME_SIZE] = {};
        ModelCooker::Settings model;
        TextureCooker::CookSettings texture;
        bool reimport = false;
        ReimportTarget target;
    };

    std::deque<Pending> queue;
    uint32_t total = 0;
    bool focus_name = false;

    bool push(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir);
    bool push_reimport(const ReimportTarget& p_target);
    void clear();
    void draw(EditorContext& ctx);
    void _enqueue(Pending&& r_pending);
    void _draw_header(Pending& r_pending);
    void _draw_model(ModelCooker::Settings& r_settings, bool p_materials);
    void _draw_texture(TextureCooker::CookSettings& r_settings);
    void _draw_collision(CollisionCooker::Settings& r_settings, bool* r_generate);
    void _draw_footer(EditorContext& ctx, Pending& r_pending);
    void _start(EditorContext& ctx, Pending& r_pending);
    void _next();
};

}
