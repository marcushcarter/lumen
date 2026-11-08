#pragma once
#include <core/assets/lmesh.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lumen {

struct MeshPageReader
{
    struct Source {
        std::filesystem::path path;
        uint32_t flags = 0;
        std::shared_ptr<const LMeshData> tables;
    };

    struct Request {
        uint32_t mesh = 0;
        uint32_t generation = 0;
        uint32_t page = 0;
        uint32_t bytes = 0;
        std::shared_ptr<const Source> source;
    };

    struct Result {
        uint32_t mesh = 0;
        uint32_t generation = 0;
        uint32_t page = 0;
        uint32_t bytes = 0;
        const char* error = nullptr;
        LMeshPageData data;
    };

    static constexpr uint32_t MAX_WORKERS = 4;
    static constexpr uint32_t COALESCE_MAX_PAGES = 16;
    static constexpr uint32_t COALESCE_SCAN = 128;
    static constexpr uint64_t COALESCE_MAX_BYTES = 1024ull * 1024;

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Request> pending;
    std::vector<Result> finished;
    std::atomic<bool> quit{ false };
    std::atomic<uint32_t> in_flight{ 0 };

    void start();
    void stop();
    void submit(std::vector<Request>& r_requests);
    uint64_t cancel(uint32_t p_mesh);
    void drain(std::vector<Result>& r_out);
    void _gather(std::vector<Request>& r_batch);
    void _run();
};

}
