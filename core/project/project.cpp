#include <core/project/project.h>
#include <core/io/path.h>
#include <drivers/toml/toml_helpers.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>

namespace lumen {

void Project::_resolve_dirs(const std::filesystem::path& p_root)
{
    root = p_root;
    data_dir = p_root / DIR_DATA;
    assets_dir = p_root / DIR_ASSETS;
    content_dir = p_root / DIR_CONTENT;
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
            return Failed;
        }
    }

    return Ok;
}

std::filesystem::path Project::content_path(Guid p_guid) const
{
    char buf[Guid::PATH_BUFFER + 4];
    p_guid.to_path_chars(buf);
    std::memcpy(buf + Guid::PATH_CHARS, ".bin", 5);
    return content_dir / buf;
}

Error Project::load(const std::filesystem::path& p_root)
{
    using enum Error;
    unload();

    std::filesystem::path file = p_root / FILE_NAME;

    std::ifstream in(file, std::ios::binary);
    if (!in) { log_write("Project: no %s in %s", FILE_NAME, p_root.string().c_str()); return Failed; }

    toml::table tbl;
    try {
        tbl = toml::parse(in);
    } catch (const toml::parse_error& e) {
        log_write("Project: failed to parse %s (%s)", file.string().c_str(), std::string(e.description()).c_str());
        return Failed;
    }

    const std::int64_t parsed_version = tbl.at_path("project.version").value_or(std::int64_t{0});
    if (parsed_version > static_cast<std::int64_t>(FORMAT_VERSION)) {
        log_write("Project: '%s' is format version %lld, this build supports %u.", file.string().c_str(), (long long)parsed_version, FORMAT_VERSION);
        return Failed;
    }

    settings.width  = tbl.at_path("window.width").value_or(settings.width);
    settings.height = tbl.at_path("window.height").value_or(settings.height);

    if (Error e = _ensure_layout(p_root); e != Ok) return e;

    _resolve_dirs(p_root);
    name = p_root.filename().string();

    return Ok;
}

void Project::unload()
{
    root.clear();
    name.clear();
    settings = {};
}

Error Project::save() const
{
    using enum Error;
    if (root.empty()) return Failed;

    std::error_code ec;
    std::filesystem::create_directories(root / DIR_DATA, ec);
    if (ec) return Failed;

    toml::table project;
    project.insert_or_assign("version", static_cast<std::int64_t>(FORMAT_VERSION));
    project.insert_or_assign("name", name);

    toml::table window;
    window.insert_or_assign("width", settings.width);
    window.insert_or_assign("height", settings.height);

    toml::table out_tbl;
    out_tbl.insert_or_assign("project", std::move(project));
    out_tbl.insert_or_assign("window", std::move(window));

    std::ofstream out(root / FILE_NAME, std::ios::binary);
    if (!out) return Failed;
    out << out_tbl << '\n';

    return Ok;
}

Error Project::create(const std::filesystem::path& p_root, std::string_view p_name)
{
    using enum Error;

    std::error_code ec;
    if (std::filesystem::exists(p_root / FILE_NAME, ec)) {
        log_write("Project: '%s' already contains a project.", p_root.string().c_str());
        return Failed;
    }

    if (Error e = _ensure_layout(p_root); e != Ok) return e;

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
        return Failed;
    }

    std::filesystem::path target = std::filesystem::weakly_canonical(p_root, ec);
    if (ec) return Failed;
    if (target == target.root_path()) {
        log_write("Project: refusing to remove filesystem root '%s'.", target.string().c_str());
        return Failed;
    }

    uintmax_t removed = std::filesystem::remove_all(target, ec);
    if (ec || removed == static_cast<uintmax_t>(-1)) {
        log_write("Project: failed to remove '%s' (%s)", target.string().c_str(), ec.message().c_str());
        return Failed;
    }

    log_write("Project destroyed: '%s' (%llu entries)", target.string().c_str(), (unsigned long long)removed);
    return Ok;
}

std::string Project::peek_name(const std::filesystem::path& p_root)
{
    std::ifstream f(p_root / FILE_NAME);
    if (!f) return p_root.filename().string();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        if (line.substr(0, sp) == "project.name")
            return line.substr(sp + 1);
    }
    return p_root.filename().string();
}

}