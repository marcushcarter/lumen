#include <editor/assets/import/asset_import_tracker.h>
#include <core/project/project.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace lumen {

bool ImportStep::cancelled() const
{
    return session && session->cancel.load(std::memory_order_relaxed);
}

void ImportStep::report(float p_t) const
{
    if (!session) return;
    const float v = base + span * std::clamp(p_t, 0.0f, 1.0f);
    float cur = session->progress.load(std::memory_order_relaxed);
    while (v > cur && !session->progress.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

ImportSession::Staged ImportSession::stage(const Project& p_project, const std::filesystem::path& p_asset, Guid p_guid, bool p_content, bool p_notify)
{
    Output o;
    o.asset = p_asset;
    o.guid = p_guid;
    o.notify = p_notify;
    o.staged.asset = staging / "assets" / p_asset.lexically_relative(assets_dir);
    if (p_content) {
        o.content_bin = p_project.content_path(p_guid);
        o.staged.content_bin = staging / "content" / o.content_bin.filename();
    }
    std::error_code ec;
    std::filesystem::create_directories(o.staged.asset.parent_path(), ec);
    if (p_content) std::filesystem::create_directories(o.staged.content_bin.parent_path(), ec);

    std::lock_guard lock(outputs_mutex);
    outputs.push_back(o);
    return o.staged;
}

void ImportSession::keep_loaded(const std::filesystem::path& p_asset)
{
    std::lock_guard lock(outputs_mutex);
    for (Output& o : outputs) {
        if (o.asset == p_asset) o.keep_loaded = true;
    }
}

void ImportSession::unstage(const std::filesystem::path& p_asset)
{
    std::lock_guard lock(outputs_mutex);
    std::erase_if(outputs, [&](const Output& o) { return o.asset == p_asset; });
}

bool ImportSession::staged_exists(const std::filesystem::path& p_asset)
{
    std::lock_guard lock(outputs_mutex);
    return std::any_of(outputs.begin(), outputs.end(), [&](const Output& o) { return o.asset == p_asset; });
}

static bool _move_replace(const std::filesystem::path& p_from, const std::filesystem::path& p_to)
{
    std::error_code ec;
    std::filesystem::create_directories(p_to.parent_path(), ec);
    std::filesystem::rename(p_from, p_to, ec);
    if (!ec) return true;
    ec.clear();
    std::filesystem::remove(p_to, ec);
    std::filesystem::rename(p_from, p_to, ec);
    if (!ec) return true;
    ec.clear();
    if (!std::filesystem::copy_file(p_from, p_to, std::filesystem::copy_options::overwrite_existing, ec) || ec) return false;
    std::filesystem::remove(p_from, ec);
    return true;
}

std::shared_ptr<ImportSession> AssetImportTracker::begin(const Project& p_project, const std::filesystem::path& p_dst)
{
    if (find(p_dst)) {
        log_write("Import: '%s' is already being imported.", p_dst.string().c_str());
        return nullptr;
    }
    auto s = std::make_shared<ImportSession>();
    s->dst = p_dst;
    s->assets_dir = p_project.assets_dir;
    s->staging = p_project.editor_dir / STAGING_DIR / Guid::generate().to_string();
    sessions.push_back(s);
    return s;
}

ImportSession* AssetImportTracker::find(const std::filesystem::path& p_dst) const
{
    for (const auto& s : sessions) {
        if (s->dst == p_dst && !s->cancel.load(std::memory_order_relaxed)) return s.get();
    }
    return nullptr;
}

bool AssetImportTracker::path_contains(const std::filesystem::path& p_parent, const std::filesystem::path& p_child)
{
    const std::filesystem::path parent = p_parent.lexically_normal();
    const std::filesystem::path child = p_child.lexically_normal();
    auto pi = parent.begin();
    auto ci = child.begin();
    for (; pi != parent.end(); ++pi, ++ci) {
        if (pi->empty() && std::next(pi) == parent.end()) break;
        if (ci == child.end() || *pi != *ci) return false;
    }
    return ci != child.end();
}

bool ImportSession::writes(const std::filesystem::path& p_path)
{
    if (dst == p_path || AssetImportTracker::path_contains(dst, p_path)) return true;
    std::lock_guard lock(outputs_mutex);
    for (const Output& o : outputs) {
        if (o.asset == p_path) return true;
    }
    return false;
}

bool ImportSession::touches(const std::filesystem::path& p_path)
{
    if (writes(p_path) || AssetImportTracker::path_contains(p_path, dst)) return true;
    std::lock_guard lock(outputs_mutex);
    for (const Output& o : outputs) {
        if (AssetImportTracker::path_contains(p_path, o.asset)) return true;
    }
    return false;
}

float AssetImportTracker::progress(const std::filesystem::path& p_path) const
{
    for (const auto& s : sessions) {
        if (s->cancel.load(std::memory_order_relaxed)) continue;
        if (s->touches(p_path)) return std::min(s->progress.load(std::memory_order_relaxed), 0.999f);
    }
    return -1.0f;
}

bool AssetImportTracker::importing(const std::filesystem::path& p_path) const
{
    for (const auto& s : sessions) {
        if (s->cancel.load(std::memory_order_relaxed)) continue;
        if (s->writes(p_path)) return true;
    }
    return false;
}

void AssetImportTracker::cancel(const std::filesystem::path& p_path)
{
    for (const auto& s : sessions) {
        if (s->touches(p_path)) s->cancel.store(true, std::memory_order_relaxed);
    }
}

void AssetImportTracker::cancel_all()
{
    for (const auto& s : sessions) s->cancel.store(true, std::memory_order_relaxed);
}

void AssetImportTracker::wait_all()
{
    static constexpr auto POLL = std::chrono::milliseconds(5);
    const auto running = [this] {
        return std::any_of(sessions.begin(), sessions.end(), [](const auto& s) { return s->state.load(std::memory_order_acquire) == ImportSession::State::RUNNING; });
    };
    while (running()) std::this_thread::sleep_for(POLL);
    tick();
}

uint32_t AssetImportTracker::active_count() const
{
    return (uint32_t)std::count_if(sessions.begin(), sessions.end(), [](const auto& s) {
        return !s->cancel.load(std::memory_order_relaxed) && s->state.load(std::memory_order_acquire) == ImportSession::State::RUNNING;
    });
}

void AssetImportTracker::_commit(ImportSession& r_session)
{
    for (const ImportSession::Output& o : r_session.outputs) {
        if (!o.content_bin.empty() && !_move_replace(o.staged.content_bin, o.content_bin)) {
            log_write("Import: failed to move '%s'; staged files kept in '%s'.", o.content_bin.string().c_str(), r_session.staging.string().c_str());
            return;
        }
    }
    for (const ImportSession::Output& o : r_session.outputs) {
        if (!_move_replace(o.staged.asset, o.asset)) {
            log_write("Import: failed to move '%s'; staged files kept in '%s'.", o.asset.string().c_str(), r_session.staging.string().c_str());
            return;
        }
        completed.push_back({ o.guid, o.asset, o.content_bin, o.notify, o.keep_loaded });
    }
    std::error_code ec;
    std::filesystem::remove_all(r_session.staging, ec);
}

void AssetImportTracker::tick()
{
    for (auto it = sessions.begin(); it != sessions.end(); ) {
        ImportSession& s = **it;
        const ImportSession::State state = s.state.load(std::memory_order_acquire);
        if (state == ImportSession::State::RUNNING) {
            ++it;
            continue;
        }
        if (s.cancel.load(std::memory_order_relaxed)) {
            log_write("Import canceled: %s", s.dst.filename().string().c_str());
        } else if (state == ImportSession::State::FAILED) {
            log_write("Import failed: %s", s.dst.filename().string().c_str());
        }
        if (state == ImportSession::State::DONE && !s.cancel.load(std::memory_order_relaxed)) {
            _commit(s);
        } else {
            std::error_code ec;
            std::filesystem::remove_all(s.staging, ec);
        }
        it = sessions.erase(it);
    }
}

std::vector<std::filesystem::path> AssetImportTracker::pending_out(const std::filesystem::path& p_folder) const
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& s : sessions) {
        if (s->cancel.load(std::memory_order_relaxed) || s->dst.parent_path() != p_folder) continue;
        if (std::filesystem::exists(s->dst, ec)) continue;
        out.push_back(s->dst);
    }
    return out;
}

void AssetImportTracker::clear_staging(const Project& p_project)
{
    std::error_code ec;
    std::filesystem::remove_all(p_project.editor_dir / STAGING_DIR, ec);
}

Error AssetImportTracker::resolve_import(const Project& p_project, AssetType p_type, const std::filesystem::path& p_dest, Guid& r_guid, std::filesystem::path& r_content_bin)
{
    using enum Error;
    std::error_code ec;
    if (std::filesystem::exists(p_dest, ec)) {
        AssetInfo existing = read_asset_info(p_dest);
        if (!existing.valid() || existing.type != p_type) return FAILED;
        r_guid = existing.guid;
    } else {
        r_guid = Guid::generate();
    }
    r_content_bin = p_project.content_path(r_guid);
    return OK;
}

}
