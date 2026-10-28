#include <core/project/project.h>
#include <core/io/path.h>
#include <core/io/file_io.h>
#include <drivers/toml/toml_helpers.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lumen {

/***************/
/**** FILES ****/
/***************/

void Project::_resolve_dirs(const std::filesystem::path& p_root)
{
    root = p_root;
    data_dir = p_root / DIR_DATA;
    assets_dir = p_root / DIR_ASSETS;
    content_dir = p_root / DIR_CONTENT;
    editor_dir = p_root / DIR_EDITOR;
}

Error Project::_ensure_layout(const std::filesystem::path& p_root)
{
    using enum Error;

    const char* dirs[] = { DIR_DATA, DIR_ASSETS, DIR_CONTENT };
    for (const char* dir : dirs) {
        std::error_code ec;
        std::filesystem::create_directories(p_root / dir, ec);
        if (ec) {
            log_write("Project: failed to create '%s' (%s)", (p_root / dir).string().c_str(), ec.message().c_str());
            return FAILED;
        }
    }

    return OK;
}

std::filesystem::path Project::content_path(Guid p_guid) const
{
    char buf[Guid::PATH_BUFFER + 4];
    p_guid.to_path_chars(buf);
    std::memcpy(buf + Guid::PATH_CHARS, ".bin", 5);
    return content_dir / buf;
}

/***************/
/**** STATE ****/
/***************/

Error Project::load(const std::filesystem::path& p_root)
{
    using enum Error;
    unload();

    std::filesystem::path file = p_root / FILE_NAME;

    std::ifstream in(file, std::ios::binary);
    if (!in) { log_write("Project: no %s in %s", FILE_NAME, p_root.string().c_str()); return FAILED; }

    toml::table tbl;
    try {
        tbl = toml::parse(in);
    } catch (const toml::parse_error& e) {
        log_write("Project: failed to parse %s (%s)", file.string().c_str(), std::string(e.description()).c_str());
        return FAILED;
    }

    const std::int64_t parsed_version = tbl.at_path("project.version").value_or(std::int64_t{0});
    if (parsed_version > static_cast<std::int64_t>(FORMAT_VERSION)) {
        log_write("Project: '%s' is format version %lld, this build supports %u.", file.string().c_str(), (long long)parsed_version, FORMAT_VERSION);
        return FAILED;
    }

    settings.width = tbl.at_path("window.width").value_or(settings.width);
    settings.height = tbl.at_path("window.height").value_or(settings.height);
    settings.window_icon = Guid::from_string(tbl.at_path("window.icon").value_or(std::string{}));
    settings.window_title = tbl.at_path("window.title").value_or(std::string{});

    if (Error e = _ensure_layout(p_root); e != OK) return e;

    _resolve_dirs(p_root);
    name = p_root.filename().string();

    return OK;
}

void Project::unload()
{
    root.clear();
    data_dir.clear();
    assets_dir.clear();
    content_dir.clear();
    editor_dir.clear();
    name.clear();
    settings = {};
}

Error Project::save() const
{
    using enum Error;
    if (root.empty()) return FAILED;

    std::error_code ec;
    std::filesystem::create_directories(root / DIR_DATA, ec);
    if (ec) return FAILED;

    toml::table project;
    project.insert_or_assign("version", static_cast<std::int64_t>(FORMAT_VERSION));
    project.insert_or_assign("name", name);

    toml::table window;
    window.insert_or_assign("width", settings.width);
    window.insert_or_assign("height", settings.height);
    window.insert_or_assign("title", settings.window_title);
    window.insert_or_assign("icon", !settings.window_icon.valid() ? std::string{} : settings.window_icon.to_string());

    toml::table out_tbl;
    out_tbl.insert_or_assign("project", std::move(project));
    out_tbl.insert_or_assign("window", std::move(window));

    std::ostringstream ss;
    ss << out_tbl << '\n';
    return write_file_atomic(root / FILE_NAME, ss.str()) ? OK : FAILED;
}

Error Project::create(const std::filesystem::path& p_root, std::string_view p_name)
{
    using enum Error;

    std::error_code ec;
    if (std::filesystem::exists(p_root / FILE_NAME, ec)) {
        log_write("Project: '%s' already contains a project.", p_root.string().c_str());
        return FAILED;
    }

    if (Error e = _ensure_layout(p_root); e != OK) return e;

    Project p;
    p._resolve_dirs(p_root);
    p.name = p_name;
    return p.save();
}

Error Project::destroy(const std::filesystem::path& p_root)
{
    using enum Error;
    std::error_code ec;

    if (!std::filesystem::exists(p_root / FILE_NAME, ec) || ec) {
        log_write("Project: '%s' is not a Lumen project; refusing to delete.", p_root.string().c_str());
        return FAILED;
    }

    std::filesystem::path target = std::filesystem::weakly_canonical(p_root, ec);
    if (ec) return FAILED;
    if (target == target.root_path()) {
        log_write("Project: refusing to remove filesystem root '%s'.", target.string().c_str());
        return FAILED;
    }

    for (auto it = std::filesystem::recursive_directory_iterator(target, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code perm_ec;
        std::filesystem::permissions(it->path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::add, perm_ec);
    }
    ec.clear();

    uintmax_t removed = std::filesystem::remove_all(target, ec);
    if (ec || removed == static_cast<uintmax_t>(-1)) {
        log_write("Project: failed to remove '%s' (%s)", target.string().c_str(), ec.message().c_str());
        return FAILED;
    }

    log_write("Project destroyed: '%s' (%llu entries)", target.string().c_str(), (unsigned long long)removed);
    return OK;
}

std::string Project::peek_name(const std::filesystem::path& p_root)
{
    return p_root.filename().string();
}

/*********************/
/**** ASSET PATHS ****/
/*********************/

std::string Project::asset_path_from(const std::filesystem::path& p_assets_dir, const std::filesystem::path& p_path)
{
    const std::filesystem::path rel = p_path.lexically_normal().lexically_relative(p_assets_dir.lexically_normal());
    if (rel.empty() || *rel.begin() == "..") return p_path.generic_string();
    if (rel == ".") return ASSET_PATH_ROOT;
    return (std::filesystem::path(ASSET_PATH_ROOT) / rel).generic_string();
}

std::filesystem::path Project::asset_path_resolve_from(const std::filesystem::path& p_assets_dir, std::string_view p_asset_path)
{
    const std::string_view root = ASSET_PATH_ROOT;
    if (!p_asset_path.starts_with(root)) return std::filesystem::path(p_asset_path);
    std::string_view rest = p_asset_path.substr(root.size());
    if (rest.empty()) return p_assets_dir;
    if (rest[0] != '/' && rest[0] != '\\') return std::filesystem::path(p_asset_path);
    rest.remove_prefix(1);
    return (p_assets_dir / rest).lexically_normal();
}

}
