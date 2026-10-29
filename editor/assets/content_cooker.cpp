#include <editor/assets/content_cooker.h>
#include <core/assets/lmaterial.h>
#include <core/assets/lmodel.h>
#include <core/project/project.h>
#include <system_error>

namespace lumen {

bool ContentCooker::_stale(const std::filesystem::path& p_source, const std::filesystem::path& p_cooked, AssetType p_type)
{
    std::error_code ec;
    const auto cooked_time = std::filesystem::last_write_time(p_cooked, ec);
    if (ec) return true;
    const auto source_time = std::filesystem::last_write_time(p_source, ec);
    if (ec || source_time > cooked_time) return true;
    LAssetHeader header{};
    return !read_asset_header(p_cooked, header) || header.type != p_type;
}

void ContentCooker::_cook_file(const Project& p_project, const std::filesystem::path& p_source, AssetType p_type, Result& r_result)
{
    LModel model;
    LMaterial material;
    Guid guid;
    if (p_type == AssetType::MODEL) {
        if (model.load(p_source) == Error::OK) guid = model.guid;
    } else {
        if (material.load(p_source) == Error::OK) guid = material.guid;
    }
    if (!guid.valid()) {
        log_write("Cook: couldn't read '%s'.", p_source.string().c_str());
        r_result.failed++;
        return;
    }

    const std::filesystem::path cooked = p_project.content_path(guid);
    if (!_stale(p_source, cooked, p_type)) {
        r_result.current++;
        return;
    }

    const Error err = p_type == AssetType::MODEL ? model.save_cooked(cooked) : material.save_cooked(cooked);
    if (err != Error::OK) {
        log_write("Cook: couldn't write '%s'.", cooked.string().c_str());
        r_result.failed++;
        return;
    }
    r_result.cooked++;
}

ContentCooker::Result ContentCooker::cook(const Project& p_project)
{
    Result result;
    if (!p_project.loaded()) return result;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(p_project.assets_dir, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext == ".lmodel") _cook_file(p_project, it->path(), AssetType::MODEL, result);
        else if (ext == ".lmaterial") _cook_file(p_project, it->path(), AssetType::MATERIAL, result);
    }
    if (ec) {
        log_write("Cook: couldn't scan '%s' (%s).", p_project.assets_dir.string().c_str(), ec.message().c_str());
        result.failed++;
    }

    if (result.cooked || result.failed) log_write("Cook: %u cooked, %u up to date, %u failed.", result.cooked, result.current, result.failed);
    return result;
}

ContentCooker::Result ContentCooker::cook_root(const std::filesystem::path& p_root)
{
    Project project;
    if (project.load(p_root, false) != Error::OK) return Result{ .failed = 1 };
    return cook(project);
}

}
