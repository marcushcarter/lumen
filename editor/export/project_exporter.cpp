#include <editor/export/project_exporter.h>
#include <core/io/embedded_resource.h>
#include <core/io/file_io.h>
#include <core/io/path.h>
#include <core/project/project.h>
#include <editor/assets/content_cooker.h>
#include <drivers/windows/dialogs_win32.h>
#include <system_error>

namespace lumen {

std::filesystem::path ProjectExporter::_setting_path(const std::string& p_setting, const std::filesystem::path& p_fallback)
{
    if (p_setting.empty()) return p_fallback;
    return std::filesystem::path(reinterpret_cast<const char8_t*>(p_setting.c_str()));
}

std::filesystem::path ProjectExporter::_folder_name(const Project& p_project)
{
    return _setting_path(p_project.settings.export_folder, p_project.root.filename().wstring() + L"_export");
}

std::filesystem::path ProjectExporter::_exe_name(const Project& p_project)
{
    std::filesystem::path exe = _setting_path(p_project.settings.export_exe, p_project.root.filename().wstring() + L"_export");
    if (exe.extension() != L".exe") exe += L".exe";
    return exe;
}

Error ProjectExporter::run(const Project& p_project, const std::filesystem::path& p_destination, std::filesystem::path& r_out)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V_MSG(!p_project.loaded(), FAILED, "Export: no project loaded.");

    const EmbeddedResource::Blob runtime = EmbeddedResource::load(RUNTIME_RESOURCE);
    LUMEN_ERR_FAIL_COND_V_MSG(!runtime, FAILED, "Export: this editor build has no embedded runtime.");

    const std::filesystem::path out = p_destination / _folder_name(p_project);
    LUMEN_ERR_FAIL_COND_V_MSG(Paths::is_under(out, p_project.data_dir), FAILED, "Export: the export folder can't be inside the project's Data folder.");

    if (ContentCooker::cook(p_project).failed) {
        log_write("Export: cooking failed, see the log above.");
        return FAILED;
    }

    std::error_code ec;
    const std::filesystem::path out_data = out / Project::DIR_DATA;
    std::filesystem::remove_all(out_data, ec);
    std::filesystem::create_directories(out_data, ec);
    if (ec) {
        log_write("Export: couldn't create '%s' (%s).", out_data.string().c_str(), ec.message().c_str());
        return FAILED;
    }

    const std::filesystem::path sources[] = { p_project.root / Project::FILE_NAME, p_project.content_dir };
    for (const std::filesystem::path& src : sources) {
        std::filesystem::copy(src, out / src.lexically_relative(p_project.root), std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            log_write("Export: couldn't copy '%s' (%s).", src.string().c_str(), ec.message().c_str());
            return FAILED;
        }
    }

    const std::filesystem::path exe = out / _exe_name(p_project);
    if (!write_file_atomic(exe, runtime.data, runtime.size)) {
        log_write("Export: couldn't write '%s'.", exe.string().c_str());
        return FAILED;
    }

    r_out = exe;
    return OK;
}

void ProjectExporter::run_with_dialog(Project& r_project)
{
    if (!r_project.loaded()) return;
    if (r_project.save() != Error::OK) log_write("Export: failed to save the project settings.");

    const std::filesystem::path destination = drivers::Win32Dialogs::open_folder(L"Export Project");
    if (destination.empty()) return;

    const std::filesystem::path folder = _folder_name(r_project);
    std::error_code ec;
    if (std::filesystem::exists(destination / folder, ec) && !std::filesystem::is_empty(destination / folder, ec)) {
        const std::wstring heading = L"Replace \"" + folder.wstring() + L"\"?";
        if (!drivers::Win32Dialogs::confirm(L"Export Project", heading.c_str(), L"The existing export's Data folder and executable are replaced. Other files in it are left alone.")) return;
    }

    std::filesystem::path exe;
    if (run(r_project, destination, exe) != Error::OK) return;
    log_write("Export: wrote '%s'.", exe.string().c_str());
    Paths::reveal_in_explorer(exe);
}

}
