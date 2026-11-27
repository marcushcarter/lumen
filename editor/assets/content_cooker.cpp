#include <editor/assets/content_cooker.h>
#include <core/assets/lmaterial.h>
#include <core/assets/llocomotion.h>
#include <core/assets/lmesh.h>
#include <core/project/project.h>
#include <core/scene/scene_file.h>
#include <core/scene/template.h>
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

void ContentCooker::_cook_mesh_materials(const Project& p_project, const std::filesystem::path& p_source, Result& r_result)
{
    Guid guid;
    std::vector<Guid> materials;
    if (!lmesh_asset_materials(p_source, guid, materials)) {
        log_write("Cook: couldn't read '%s'.", p_source.string().c_str());
        r_result.failed++;
        return;
    }
    const std::filesystem::path bin = p_project.content_path(guid);
    std::vector<Guid> current;
    lmesh_read_materials(bin, current);
    if (current == materials) {
        r_result.current++;
        return;
    }
    if (!lmesh_write_materials(bin, materials)) {
        log_write("Cook: couldn't write the materials into '%s'.", bin.string().c_str());
        r_result.failed++;
        return;
    }
    r_result.cooked++;
}

void ContentCooker::_cook_file(const Project& p_project, const std::filesystem::path& p_source, AssetType p_type, Result& r_result)
{
    LMaterial material;
    Guid guid;
    if (material.load(p_source) == Error::OK) guid = material.guid;
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

    const Error err = material.save_cooked(cooked);
    if (err != Error::OK) {
        log_write("Cook: couldn't write '%s'.", cooked.string().c_str());
        r_result.failed++;
        return;
    }
    r_result.cooked++;
}

void ContentCooker::_cook_copy(const Project& p_project, const std::filesystem::path& p_source, const char* p_extension, Result& r_result)
{
    const AssetInfo info = read_asset_info(p_source);
    if (!info.valid() || !info.guid.valid()) {
        log_write("Cook: couldn't read '%s'.", p_source.string().c_str());
        r_result.failed++;
        return;
    }
    const std::filesystem::path cooked = p_project.content_path(info.guid, p_extension);
    std::error_code ec;
    const auto cooked_time = std::filesystem::last_write_time(cooked, ec);
    if (!ec && cooked_time >= std::filesystem::last_write_time(p_source, ec) && !ec) {
        r_result.current++;
        return;
    }
    std::filesystem::create_directories(cooked.parent_path(), ec);
    if (!std::filesystem::copy_file(p_source, cooked, std::filesystem::copy_options::overwrite_existing, ec)) {
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
        if (ext == ".lmesh") _cook_mesh_materials(p_project, it->path(), result);
        else if (ext == ".lmaterial") _cook_file(p_project, it->path(), AssetType::MATERIAL, result);
        else if (ext == SceneFile::EXTENSION) _cook_copy(p_project, it->path(), SceneFile::EXTENSION, result);
        else if (ext == TemplateLibrary::EXTENSION) _cook_copy(p_project, it->path(), TemplateLibrary::EXTENSION, result);
        else if (ext == LLocomotionSet::EXTENSION) _cook_copy(p_project, it->path(), LLocomotionSet::EXTENSION, result);
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
