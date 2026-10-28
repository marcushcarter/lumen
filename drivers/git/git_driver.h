#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen::drivers {

struct GitDriver
{
    static constexpr const char* REMOTE = "origin";
    static constexpr const char* BRANCH_PREFIX = "branch-";
    static constexpr double STALE_LOCK_SECONDS = 10.0;

    enum class State : uint8_t { CHECKING, NO_GIT, NO_REPO, NO_REMOTE, READY };
    enum class Problem : uint8_t { NONE, FAILED, REMOTE_MISSING, BAD_URL, UNRELATED, AUTH, OFFLINE, REJECTED, DIVERGED, LOCKED, NAME_TAKEN, INVALID_NAME, NO_FOLDER };
    enum class Operation : uint8_t { NONE, MERGE, REBASE, CHERRY_PICK, REVERT };

    struct Step {
        std::vector<std::string> args;
        std::string input;
    };

    struct Commit {
        std::string hash;
        std::vector<std::string> parents;
        std::string author;
        std::string summary;
        int64_t time;
    };

    struct Ref {
        std::string name;
        std::string hash;
        bool remote;
    };

    struct Status {
        State state = State::CHECKING;
        std::string user_name;
        std::string user_email;
        std::string branch;
        std::string head;
        bool detached = false;
        std::string remote_url;
        std::vector<std::string> changes;
        std::vector<Commit> commits;
        std::vector<Ref> refs;
        std::vector<std::string> unuploaded;
        int32_t incoming = 0;
        Problem problem = Problem::NONE;
        std::string error;
        Operation operation = Operation::NONE;
        bool failed = false;
        uint64_t version = 0;
    };

    std::filesystem::path repo_root;
    std::thread worker;
    std::thread poll_worker;
    std::atomic<bool> busy = false;
    std::atomic<bool> polling = false;
    std::function<void(const std::string&)> log_callback;
    // patterns the repo's .gitignore must contain, appended when missing (set before initialize)
    std::vector<std::string> ignore_lines;

    std::mutex mutex;
    Status status;
    std::string ref_signature;

    void initialize(const std::filesystem::path& p_root);
    void shutdown();

    bool refresh();
    bool identity_set(const std::string& p_name, const std::string& p_email);
    bool repo_create(const std::string& p_project_name);
    bool remote_set(const std::string& p_url);
    bool remote_remove();
    bool lock_clear();
    void error_dismiss();

    bool commit(const std::string& p_message);
    bool upload();
    bool fetch();
    bool changes_poll();
    std::vector<std::string> changes_now();
    bool branch_rename(const std::string& p_old, const std::string& p_new);
    bool branch_delete(const std::string& p_name);

    bool commit_now(const std::string& p_message);
    bool go_to_now(const std::string& p_hash);
    bool discard_now();
    bool download_now();
    bool branch_delete_now(const std::string& p_name, bool p_move_to_main);
    bool commit_delete_now(const std::string& p_branch);
    bool operation_abort_now();
    bool repo_init_now(const std::filesystem::path& p_root, const std::string& p_project_name);

    static bool has_repo(State p_state) { return p_state == State::NO_REMOTE || p_state == State::READY; }
    static const char* operation_name(Operation p_operation);

    void _ignore_ensure();
    std::vector<Step> _commit_steps(const std::string& p_message);
    std::string _main_branch() const;
    bool _git_dir_exists() const;
    Operation _operation_detect() const;
    std::string _signature();

    bool _start(std::vector<Step> p_steps);
    bool _start_job(std::function<bool()> p_job, bool p_report);
    bool _run_now(std::vector<Step> p_steps);
    bool _run_job_now(std::function<bool()> p_job);
    bool _exec(const std::vector<Step>& p_steps, std::string* r_output = nullptr);
    void _fail(Problem p_problem, const std::string& p_error);
    void _finish(bool p_ok, bool p_report);
    void _refresh_sync();
    bool _run(const Step& p_step, std::string& r_output);
    void _log(const Step& p_step, const std::string& p_output);
    void _join();

    static std::vector<std::string> _parse_changes(const std::string& p_porcelain);
    static Problem _classify(const std::string& p_output);
    static std::string _message(Problem p_problem, const std::string& p_output);
};

}