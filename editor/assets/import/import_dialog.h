#pragma once
#include <editor/assets/import/model_cooker.h>
#include <filesystem>
#include <deque>

namespace lumen {

struct EditorContext;

enum class ImportKind : uint8_t { NONE, TEXTURE, MODEL };

ImportKind import_kind(const std::filesystem::path& p_src);

struct ImportDialog
{
    static constexpr size_t NAME_SIZE = 128;

    struct Pending {
        std::filesystem::path source;
        std::filesystem::path dst_dir;
        ImportKind kind = ImportKind::NONE;
        char name[NAME_SIZE] = {};
        ModelCooker::Settings model;
    };

    std::deque<Pending> queue;
    uint32_t total = 0;
    // bool _focus_pending = false;
    bool focus_name = false;

    bool push(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir);
    void clear();
    void draw(EditorContext& ctx);
    void _draw_header(Pending& r_pending);
    void _draw_model(ModelCooker::Settings& r_settings);
    // void _draw_footer(EditorContext& ctx, Pending& r_pending, bool p_closed);
    void _draw_footer(EditorContext& ctx, Pending& r_pending);
    void _start(EditorContext& ctx, Pending& r_pending);
    void _next();
};

}
