#pragma once
#include <core/assets/guid.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>

namespace lumen {

struct Project;

struct ImportControl;

struct ImportChild
{
    std::filesystem::path dst;
    Guid guid;
    std::filesystem::path content_bin;
    std::shared_ptr<ImportControl> control;
};

struct ImportControl
{
    std::atomic<float> progress{ 0.0f };
    std::atomic<bool> cancel{ false };
    std::mutex children_mutex;
    std::vector<ImportChild> children;

    void push_child(ImportChild p_child) {
        std::lock_guard lock(children_mutex);
        children.push_back(std::move(p_child));
    }
};

struct AssetImportTracker
{
    struct Pending {
        std::shared_ptr<ImportControl> progress;
        Guid guid;
        std::filesystem::path content_bin;
        bool notify = true;
    };

    struct Completed {
        Guid guid;
        std::filesystem::path dst;
        std::filesystem::path content_bin;
        bool notify = true;
    };

    std::unordered_map<std::filesystem::path, Pending> pending;
    std::vector<Completed> completed;

    std::shared_ptr<ImportControl> add(const std::filesystem::path& p_dst, Guid p_guid, const std::filesystem::path& p_content_bin, std::shared_ptr<ImportControl> p_shared = nullptr, bool p_notify = true);
    float progress(const std::filesystem::path& p_dst);
    void tick();

    std::vector<std::filesystem::path> pending_out(const std::filesystem::path& p_folder) const;

    static Error resolve_import(const Project& p_project, AssetType p_type, const std::filesystem::path& p_dest, Guid& r_guid, std::filesystem::path& r_content_bin);
};

}
