#pragma once
#include <core/assets/guid.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <filesystem>
#include <memory>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace lumen {

struct Project;
struct ImportSession;

struct ImportStep
{
    ImportSession* session = nullptr;
    float base = 0.0f;
    float span = 1.0f;

    bool cancelled() const;
    void report(float p_t) const;
    ImportStep sub(float p_from, float p_to) const { return { session, base + span * p_from, span * (p_to - p_from) }; }
};

struct ImportSession
{
    enum class State : uint8_t { RUNNING, DONE, FAILED };

    struct Staged {
        std::filesystem::path asset;
        std::filesystem::path content_bin;
    };

    struct Output {
        Staged staged;
        std::filesystem::path asset;
        std::filesystem::path content_bin;
        Guid guid;
        bool notify = false;
        bool keep_loaded = false;
    };

    std::filesystem::path dst;
    std::filesystem::path assets_dir;
    std::filesystem::path staging;
    std::atomic<float> progress{ 0.0f };
    std::atomic<bool> cancel{ false };
    std::atomic<State> state{ State::RUNNING };
    std::mutex outputs_mutex;
    std::vector<Output> outputs;

    Staged stage(const Project& p_project, const std::filesystem::path& p_asset, Guid p_guid, bool p_content, bool p_notify);
    void unstage(const std::filesystem::path& p_asset);
    void keep_loaded(const std::filesystem::path& p_asset);
    bool staged_exists(const std::filesystem::path& p_asset);
    bool writes(const std::filesystem::path& p_path);
    bool touches(const std::filesystem::path& p_path);
    void finish(Error p_err) { state.store(p_err == Error::OK ? State::DONE : State::FAILED, std::memory_order_release); }
    ImportStep step() { return { this, 0.0f, 1.0f }; }
};

struct AssetImportTracker
{
    static constexpr const char* STAGING_DIR = "Import";

    struct Completed {
        Guid guid;
        std::filesystem::path dst;
        std::filesystem::path content_bin;
        bool notify = true;
        bool keep_loaded = false;
    };

    std::vector<std::shared_ptr<ImportSession>> sessions;
    std::vector<Completed> completed;

    std::shared_ptr<ImportSession> begin(const Project& p_project, const std::filesystem::path& p_dst);
    ImportSession* find(const std::filesystem::path& p_dst) const;
    float progress(const std::filesystem::path& p_path) const;
    bool importing(const std::filesystem::path& p_path) const;
    static bool path_contains(const std::filesystem::path& p_parent, const std::filesystem::path& p_child);
    void cancel(const std::filesystem::path& p_path);
    void cancel_all();
    void wait_all();
    uint32_t active_count() const;
    bool busy() const { return !sessions.empty(); }
    void tick();
    void _commit(ImportSession& r_session);

    std::vector<std::filesystem::path> pending_out(const std::filesystem::path& p_folder) const;

    static void clear_staging(const Project& p_project);
    static Error resolve_import(const Project& p_project, AssetType p_type, const std::filesystem::path& p_dest, Guid& r_guid, std::filesystem::path& r_content_bin);
};

}
