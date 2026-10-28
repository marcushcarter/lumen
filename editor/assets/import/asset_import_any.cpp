#include <editor/assets/import/asset_import_any.h>
#include <editor/assets/import/texture_cooker.h>
#include <editor/assets/import/model_importer.h>
#include <algorithm>
#include <string>

namespace lumen {

static std::string _lower_ext(const std::filesystem::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return e;
}

bool is_importable(const std::filesystem::path& p_src)
{
    const std::string e = _lower_ext(p_src);
    return
        e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || // images
        e == ".fbx" || e == ".gltf" || e == ".glb" || e == ".obj" || e == ".dae" || e == ".stl" || e == ".ply" || e == ".3ds" || e == ".blend"; // models
}

bool asset_import_any(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst)
{
    const std::string e = _lower_ext(p_src);

    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp") {
        const std::filesystem::path dst = p_dst / (p_src.stem().wstring() + L".ltexture");
        TextureCooker::CookSettings settings;
        settings.usage = texture_usage_guess(p_src);
        TextureCooker::import_async(ctx, p_src, dst, settings);
        return true;
    }

    if (e == ".fbx" || e == ".gltf" || e == ".glb" || e == ".obj" || e == ".dae" || e == ".stl" || e == ".ply" || e == ".3ds" || e == ".blend") {
        ModelImporter::import_async(ctx, p_src, p_dst);
        return true;
    }

    return false;
}

}
