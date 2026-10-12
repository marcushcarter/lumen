#pragma once
#include <core/base/error.h>
#include <filesystem>
#include <cstdint>

namespace lumen {

struct PlaySession
{
    static constexpr uint32_t STOP_TIMEOUT_MS = 3000;

    void* job = nullptr;
    void* process = nullptr;
    uint32_t process_id = 0;

    bool running = false;
    bool stopping = false;
    bool terminated = false;
    uint64_t stop_deadline_ms = 0;

    bool last_crashed = false;
    uint32_t last_exit_code = 0;

    Error initialize();
    void shutdown();

    Error launch(const std::filesystem::path& p_project_root);
    void stop();
    void poll();

    bool is_running() const { return running; }

    void _close_handles();
    void _on_exit();
};

}
