#include <drivers/git/git_driver.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace lumen::drivers {

static std::wstring _widen(const std::string& p_s)
{
    if (p_s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, p_s.data(), (int)p_s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p_s.data(), (int)p_s.size(), w.data(), n);
    return w;
}

static void _append_quoted(std::wstring& r_cmd, const std::wstring& p_arg)
{
    r_cmd += L" \"";
    size_t slashes = 0;
    for (const wchar_t c : p_arg) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        if (c == L'"') r_cmd.append(slashes * 2 + 1, L'\\');
        else r_cmd.append(slashes, L'\\');
        slashes = 0;
        r_cmd += c;
    }
    r_cmd.append(slashes * 2, L'\\');
    r_cmd += L'"';
}

static void _trim(std::string& r_s)
{
    while (!r_s.empty() && std::isspace((unsigned char)r_s.back())) r_s.pop_back();
    size_t i = 0;
    while (i < r_s.size() && std::isspace((unsigned char)r_s[i])) i++;
    r_s.erase(0, i);
}

static std::string _trimmed(std::string p_s)
{
    _trim(p_s);
    return p_s;
}

static std::string _lower(std::string p_s)
{
    for (char& c : p_s) c = (char)std::tolower((unsigned char)c);
    return p_s;
}

static void _write_if_missing(const std::filesystem::path& p_path, const std::string& p_text)
{
    std::error_code ec;
    if (std::filesystem::exists(p_path, ec)) return;
    std::ofstream f(p_path, std::ios::binary);
    f << p_text;
}

void GitDriver::_ignore_ensure()
{
    if (ignore_lines.empty() || repo_root.empty()) return;
    const std::filesystem::path path = repo_root / ".gitignore";
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::string append;
    for (const std::string& line : ignore_lines) {
        bool found = false;
        size_t start = 0;
        while (!found && start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string_view l(text.data() + start, end - start);
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.remove_suffix(1);
            found = l == line;
            start = end + 1;
        }
        if (!found) append += line + "\n";
    }
    if (append.empty()) return;
    if (!text.empty() && text.back() != '\n') append.insert(0, "\n");
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << append;
}

static std::vector<std::string> _split(const std::string& p_s, char p_sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= p_s.size()) {
        const size_t end = p_s.find(p_sep, start);
        if (end == std::string::npos) {
            out.push_back(p_s.substr(start));
            break;
        }
        out.push_back(p_s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

void GitDriver::initialize(const std::filesystem::path& p_root)
{
    _join();
    repo_root = p_root;
    SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");
    {
        std::lock_guard lock(mutex);
        status = {};
        ref_signature.clear();
    }
    // existing repos pick up new ignore patterns too, so editor-only files never show up as changes
    if (_git_dir_exists()) _ignore_ensure();
    refresh();
}

void GitDriver::shutdown()
{
    _join();
    repo_root.clear();
    std::lock_guard lock(mutex);
    status = {};
    ref_signature.clear();
}

bool GitDriver::refresh()
{
    return _start_job([] { return true; }, false);
}

bool GitDriver::identity_set(const std::string& p_name, const std::string& p_email)
{
    const std::string name = _trimmed(p_name);
    const std::string email = _trimmed(p_email);
    if (name.empty() || email.empty()) return false;
    return _start_job([this, name, email] {
        if (!_exec({ { { "config", "--global", "user.name", name }, {} }, { { "config", "--global", "user.email", email }, {} } })) return false;
        std::string head;
        if (_git_dir_exists() && !_run({ { "rev-parse", "--verify", "HEAD" }, {} }, head)) {
            return _exec({ { { "add", "-A" }, {} }, { { "commit", "-F", "-" }, "Initial commit" } });
        }
        return true;
    }, true);
}

bool GitDriver::repo_create(const std::string& p_project_name)
{
    {
        std::lock_guard lock(mutex);
        if (status.state != State::NO_REPO || busy) return false;
        if (status.user_name.empty() || status.user_email.empty()) return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(repo_root, ec)) {
        _fail(Problem::NO_FOLDER, _message(Problem::NO_FOLDER, {}));
        return false;
    }
    _write_if_missing(repo_root / "README.md", "# " + p_project_name + "\n");
    _write_if_missing(repo_root / ".gitignore", "");
    _ignore_ensure();
    return _start({
        { { "init", "-b", "main" }, {} },
        { { "config", "core.fsmonitor", "true" }, {} },
        { { "config", "core.untrackedCache", "true" }, {} },
        { { "config", "core.longpaths", "true" }, {} },
        { { "add", "-A" }, {} },
        { { "commit", "-F", "-" }, "Initial commit" },
    });
}

bool GitDriver::remote_set(const std::string& p_url)
{
    const std::string url = _trimmed(p_url);
    if (url.empty()) return false;
    std::string old;
    {
        std::lock_guard lock(mutex);
        if (!has_repo(status.state)) return false;
        old = status.remote_url;
        if (url == old) {
            status.problem = Problem::NONE;
            status.error.clear();
            status.version++;
            return true;
        }
    }
    return _start_job([this, url, old] {
        std::string heads;
        if (!_exec({ { { "ls-remote", "--heads", url }, {} } }, &heads)) {
            Problem p;
            {
                std::lock_guard lock(mutex);
                p = status.problem;
            }
            if (p == Problem::REMOTE_MISSING || p == Problem::FAILED) _fail(Problem::BAD_URL, _message(Problem::BAD_URL, {}));
            return false;
        }

        _trim(heads);
        std::string head;
        if (!heads.empty() && _run({ { "rev-parse", "HEAD" }, {} }, head)) {
            const std::string probe = "refs/lumen-probe/";
            if (!_exec({ { { "fetch", "--no-tags", url, "+refs/heads/*:" + probe + "*" }, {} } })) return false;
            std::string probe_refs;
            _run({ { "for-each-ref", "--format=%(refname)%1f%(objectname)", probe }, {} }, probe_refs);
            bool related = false;
            for (const std::string& line : _split(probe_refs, '\n')) {
                std::vector<std::string> f = _split(line, '\x1f');
                if (f.size() < 2) continue;
                _trim(f[1]);
                std::string base;
                related |= _run({ { "merge-base", "HEAD", f[1] }, {} }, base);
                std::string ignored;
                _run({ { "update-ref", "-d", f[0] }, {} }, ignored);
            }
            if (!related) {
                _fail(Problem::UNRELATED, _message(Problem::UNRELATED, {}));
                return false;
            }
        }

        if (old.empty()) {
            if (!_exec({ { { "remote", "add", REMOTE, url }, {} } })) return false;
        } else {
            if (!_exec({ { { "remote", "set-url", REMOTE, url }, {} } })) return false;
        }
        if (!heads.empty()) _exec({ { { "fetch", "--prune", REMOTE }, {} } });
        return true;
    }, true);
}

bool GitDriver::remote_remove()
{
    {
        std::lock_guard lock(mutex);
        if (status.remote_url.empty()) return false;
    }
    return _start({ { { "remote", "remove", REMOTE }, {} } });
}

bool GitDriver::lock_clear()
{
    return _start_job([this] {
        const std::filesystem::path lock_path = repo_root / ".git" / "index.lock";
        std::error_code ec;
        if (!std::filesystem::exists(lock_path, ec)) return true;
        const auto age = std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(lock_path, ec);
        if (ec || std::chrono::duration<double>(age).count() < STALE_LOCK_SECONDS) {
            _fail(Problem::LOCKED, "Git is still working on something. Try again in a few seconds.");
            return false;
        }
        std::filesystem::remove(lock_path, ec);
        if (ec) {
            _fail(Problem::LOCKED, "The git lock file couldn't be removed. Close other git tools and try again.");
            return false;
        }
        if (log_callback) log_callback("removed stale .git/index.lock");
        return true;
    }, true);
}

void GitDriver::error_dismiss()
{
    std::lock_guard lock(mutex);
    status.problem = Problem::NONE;
    status.error.clear();
    status.version++;
}

bool GitDriver::commit(const std::string& p_message)
{
    const std::string message = _trimmed(p_message);
    if (message.empty()) return false;
    {
        std::lock_guard lock(mutex);
        if (!has_repo(status.state)) return false;
    }
    return _start(_commit_steps(message));
}

bool GitDriver::upload()
{
    {
        std::lock_guard lock(mutex);
        if (status.state != State::READY) return false;
    }
    return _start({ { { "push", "--all", "-u", REMOTE }, {} } });
}

bool GitDriver::fetch()
{
    {
        std::lock_guard lock(mutex);
        if (status.state != State::READY) return false;
    }
    return _start({ { { "fetch", "--prune", REMOTE }, {} } });
}

bool GitDriver::branch_rename(const std::string& p_old, const std::string& p_new)
{
    std::string name = _trimmed(p_new);
    std::replace(name.begin(), name.end(), ' ', '-');
    if (p_old.empty() || name.empty() || name == p_old || p_old == "main" || p_old == "master") return false;
    bool uploaded = false;
    {
        std::lock_guard lock(mutex);
        bool local = false;
        bool taken = false;
        const std::string remote_old = std::string(REMOTE) + "/" + p_old;
        const std::string remote_new = std::string(REMOTE) + "/" + name;
        for (const Ref& r : status.refs) {
            local |= !r.remote && r.name == p_old;
            uploaded |= r.remote && r.name == remote_old;
            taken |= (!r.remote && r.name == name) || (r.remote && r.name == remote_new);
        }
        if (!local) return false;
        if (taken) {
            status.problem = Problem::NAME_TAKEN;
            status.error = "A branch called \"" + name + "\" already exists.";
            status.version++;
            return false;
        }
    }
    return _start_job([this, p_old, name, uploaded] {
        std::string out;
        if (!_run({ { "check-ref-format", "--branch", name }, {} }, out)) {
            _fail(Problem::INVALID_NAME, "\"" + name + "\" isn't a valid branch name. Avoid characters like ~ ^ : ? * [ and \\.");
            return false;
        }
        std::vector<Step> steps = { { { "branch", "-m", p_old, name }, {} } };
        if (uploaded) {
            steps.push_back({ { "push", REMOTE, "--delete", p_old }, {} });
            steps.push_back({ { "push", "-u", REMOTE, name }, {} });
        }
        return _exec(steps);
    }, true);
}

bool GitDriver::branch_delete(const std::string& p_name)
{
    bool local = false;
    bool uploaded = false;
    {
        std::lock_guard lock(mutex);
        if (p_name.empty() || p_name == status.branch || p_name == "main" || p_name == "master") return false;
        const std::string remote = std::string(REMOTE) + "/" + p_name;
        for (const Ref& r : status.refs) {
            local |= !r.remote && r.name == p_name;
            uploaded |= r.remote && r.name == remote;
        }
    }
    std::vector<Step> steps;
    if (local) steps.push_back({ { "branch", "-D", p_name }, {} });
    if (uploaded) steps.push_back({ { "push", REMOTE, "--delete", p_name }, {} });
    if (steps.empty()) return false;
    return _start(std::move(steps));
}

bool GitDriver::commit_now(const std::string& p_message)
{
    const std::string message = _trimmed(p_message);
    if (message.empty()) return false;
    return _run_now(_commit_steps(message));
}

bool GitDriver::go_to_now(const std::string& p_hash)
{
    std::vector<Step> steps;
    {
        std::lock_guard lock(mutex);
        const Ref* local = nullptr;
        for (const Ref& r : status.refs) {
            if (r.remote || r.hash != p_hash) continue;
            const bool better = !local || r.name == status.branch || ((r.name == "main" || r.name == "master") && local->name != status.branch);
            if (better) local = &r;
        }
        if (local) {
            if (local->name == status.branch && !status.detached) return true;
            steps.push_back({ { "switch", local->name }, {} });
        } else {
            const std::string prefix = std::string(REMOTE) + "/";
            std::string track;
            for (const Ref& r : status.refs) {
                if (!r.remote || r.hash != p_hash || !r.name.starts_with(prefix)) continue;
                const std::string logical = r.name.substr(prefix.size());
                bool exists = false;
                for (const Ref& o : status.refs) exists |= !o.remote && o.name == logical;
                if (!exists) {
                    track = r.name;
                    break;
                }
            }
            if (!track.empty()) steps.push_back({ { "switch", "--track", track }, {} });
            else steps.push_back({ { "switch", "--detach", p_hash }, {} });
        }
    }
    return _run_now(std::move(steps));
}

bool GitDriver::discard_now()
{
    return _run_now({ { { "reset", "--hard" }, {} }, { { "clean", "-fd" }, {} } });
}

bool GitDriver::download_now()
{
    return _run_job_now([this] {
        std::vector<Ref> refs;
        std::string current;
        {
            std::lock_guard lock(mutex);
            refs = status.refs;
            current = status.detached ? std::string() : status.branch;
        }
        const std::string prefix = std::string(REMOTE) + "/";
        std::vector<Step> steps;
        std::string diverged;
        for (const Ref& local : refs) {
            if (local.remote) continue;
            for (const Ref& remote : refs) {
                if (!remote.remote || remote.name != prefix + local.name || remote.hash == local.hash) continue;
                std::string out;
                if (_run({ { "merge-base", "--is-ancestor", local.hash, remote.hash }, {} }, out)) {
                    if (local.name == current) steps.push_back({ { "merge", "--ff-only", remote.name }, {} });
                    else steps.push_back({ { "branch", "-f", local.name, remote.name }, {} });
                } else if (!_run({ { "merge-base", "--is-ancestor", remote.hash, local.hash }, {} }, out)) {
                    diverged += (diverged.empty() ? "" : ", ") + local.name;
                }
            }
        }
        if (!_exec(steps)) return false;
        if (!diverged.empty()) {
            _fail(Problem::DIVERGED, diverged + " has different commits here and on GitHub. Merging isn't supported yet, so it was left as it is.");
            return false;
        }
        return true;
    });
}

bool GitDriver::branch_delete_now(const std::string& p_name, bool p_move_to_main)
{
    std::vector<Step> steps;
    {
        std::lock_guard lock(mutex);
        if (p_name.empty() || p_name == "main" || p_name == "master") return false;
        bool local = false;
        bool uploaded = false;
        const std::string remote = std::string(REMOTE) + "/" + p_name;
        for (const Ref& r : status.refs) {
            local |= !r.remote && r.name == p_name;
            uploaded |= r.remote && r.name == remote;
        }
        if (p_move_to_main || (status.branch == p_name && !status.detached)) steps.push_back({ { "switch", _main_branch() }, {} });
        if (local) steps.push_back({ { "branch", "-D", p_name }, {} });
        if (uploaded) steps.push_back({ { "push", REMOTE, "--delete", p_name }, {} });
    }
    return _run_now(std::move(steps));
}

bool GitDriver::commit_delete_now(const std::string& p_branch)
{
    std::vector<Step> steps;
    {
        std::lock_guard lock(mutex);
        if (p_branch.empty()) return false;
        bool uploaded = false;
        const std::string remote = std::string(REMOTE) + "/" + p_branch;
        for (const Ref& r : status.refs) uploaded |= r.remote && r.name == remote;
        if (status.branch != p_branch || status.detached) steps.push_back({ { "switch", p_branch }, {} });
        steps.push_back({ { "reset", "--hard", "HEAD~1" }, {} });
        if (uploaded) steps.push_back({ { "push", "--force-with-lease", REMOTE, p_branch }, {} });
    }
    return _run_now(std::move(steps));
}

bool GitDriver::operation_abort_now()
{
    const Operation op = _operation_detect();
    if (op == Operation::NONE) return true;
    return _run_now({ { { operation_name(op), "--abort" }, {} } });
}

bool GitDriver::repo_init_now(const std::filesystem::path& p_root, const std::string& p_project_name)
{
    _join();
    if (busy.exchange(true)) return false;
    repo_root = p_root;
    {
        std::lock_guard lock(mutex);
        status = {};
        ref_signature.clear();
    }

    bool ok = false;
    std::string out;
    if (!_run({ { "--version" }, {} }, out)) {
        if (log_callback) log_callback("Git isn't installed, so the project was created without version control.");
    } else if (_git_dir_exists()) {
        ok = true;
    } else {
        _write_if_missing(repo_root / "README.md", "# " + p_project_name + "\n");
        _write_if_missing(repo_root / ".gitignore", "");
        _ignore_ensure();
        std::vector<Step> steps = {
            { { "init", "-b", "main" }, {} },
            { { "config", "core.fsmonitor", "true" }, {} },
            { { "config", "core.untrackedCache", "true" }, {} },
            { { "config", "core.longpaths", "true" }, {} },
        };
        std::string name;
        std::string email;
        _run({ { "config", "--global", "user.name" }, {} }, name);
        _run({ { "config", "--global", "user.email" }, {} }, email);
        _trim(name);
        _trim(email);
        if (!name.empty() && !email.empty()) {
            steps.push_back({ { "add", "-A" }, {} });
            steps.push_back({ { "commit", "-F", "-" }, "Initial commit" });
        } else if (log_callback) {
            log_callback("Git name/email aren't set; the first commit is made once they're entered in Version Control.");
        }
        ok = _exec(steps);
    }
    busy = false;
    return ok;
}

const char* GitDriver::operation_name(Operation p_operation)
{
    switch (p_operation) {
        case Operation::MERGE: return "merge";
        case Operation::REBASE: return "rebase";
        case Operation::CHERRY_PICK: return "cherry-pick";
        case Operation::REVERT: return "revert";
        case Operation::NONE: break;
    }
    return "";
}

bool GitDriver::changes_poll()
{
    if (repo_root.empty() || busy || polling.exchange(true)) return false;
    if (poll_worker.joinable()) poll_worker.join();
    poll_worker = std::thread([this] {
        State state;
        std::string signature;
        {
            std::lock_guard lock(mutex);
            state = status.state;
            signature = ref_signature;
        }
        bool folder_flagged;
        {
            std::lock_guard lock(mutex);
            folder_flagged = status.problem == Problem::NO_FOLDER;
        }
        std::error_code ec;
        const bool folder = std::filesystem::is_directory(repo_root, ec);
        const bool git_dir = _git_dir_exists();
        if (state == State::NO_GIT || state == State::CHECKING) {
            polling = false;
            return;
        }
        if (folder == folder_flagged || git_dir != has_repo(state) || (git_dir && _signature() != signature)) _refresh_sync();
        else if (git_dir) changes_now();
        polling = false;
    });
    return true;
}

std::vector<std::string> GitDriver::changes_now()
{
    std::vector<std::string> changes;
    if (repo_root.empty() || !_git_dir_exists()) return changes;
    std::string out;
    if (!_run({ { "--no-optional-locks", "status", "--porcelain=v1", "-z" }, {} }, out)) return changes;
    changes = _parse_changes(out);
    std::lock_guard lock(mutex);
    if (status.changes != changes) {
        status.changes = changes;
        status.version++;
    }
    return changes;
}

std::vector<GitDriver::Step> GitDriver::_commit_steps(const std::string& p_message)
{
    std::vector<Step> steps;
    {
        std::lock_guard lock(mutex);
        if (status.detached) {
            uint32_t n = 1;
            std::string name;
            for (;; n++) {
                name = std::string(BRANCH_PREFIX) + std::to_string(n);
                const std::string remote = std::string(REMOTE) + "/" + name;
                bool used = false;
                for (const Ref& r : status.refs) used |= r.name == name || r.name == remote;
                if (!used) break;
            }
            steps.push_back({ { "switch", "-c", name }, {} });
        }
    }
    steps.push_back({ { "add", "-A" }, {} });
    steps.push_back({ { "commit", "-F", "-" }, p_message });
    return steps;
}

std::string GitDriver::_main_branch() const
{
    for (const char* name : { "main", "master" }) {
        for (const Ref& r : status.refs) {
            if (!r.remote && r.name == name) return name;
        }
    }
    return "main";
}

bool GitDriver::_git_dir_exists() const
{
    std::error_code ec;
    return !repo_root.empty() && std::filesystem::exists(repo_root / ".git", ec);
}

GitDriver::Operation GitDriver::_operation_detect() const
{
    std::error_code ec;
    const std::filesystem::path git = repo_root / ".git";
    if (std::filesystem::exists(git / "MERGE_HEAD", ec)) return Operation::MERGE;
    if (std::filesystem::exists(git / "rebase-merge", ec) || std::filesystem::exists(git / "rebase-apply", ec)) return Operation::REBASE;
    if (std::filesystem::exists(git / "CHERRY_PICK_HEAD", ec)) return Operation::CHERRY_PICK;
    if (std::filesystem::exists(git / "REVERT_HEAD", ec)) return Operation::REVERT;
    return Operation::NONE;
}

std::string GitDriver::_signature()
{
    std::string sig;
    _run({ { "rev-parse", "HEAD" }, {} }, sig);
    _run({ { "for-each-ref", "--format=%(refname)%(objectname)", "refs/heads", "refs/remotes" }, {} }, sig);
    _run({ { "remote", "get-url", REMOTE }, {} }, sig);
    sig += operation_name(_operation_detect());
    return sig;
}

bool GitDriver::_start(std::vector<Step> p_steps)
{
    return _start_job([this, steps = std::move(p_steps)] { return _exec(steps); }, true);
}

bool GitDriver::_start_job(std::function<bool()> p_job, bool p_report)
{
    if (repo_root.empty() || busy.exchange(true)) return false;
    if (worker.joinable()) worker.join();
    worker = std::thread([this, job = std::move(p_job), p_report] {
        const bool ok = job();
        _refresh_sync();
        _finish(ok, p_report);
        busy = false;
    });
    return true;
}

bool GitDriver::_run_now(std::vector<Step> p_steps)
{
    return _run_job_now([this, steps = std::move(p_steps)] { return _exec(steps); });
}

bool GitDriver::_run_job_now(std::function<bool()> p_job)
{
    _join();
    if (repo_root.empty() || busy.exchange(true)) return false;
    const bool ok = p_job();
    _refresh_sync();
    _finish(ok, true);
    busy = false;
    return ok;
}

bool GitDriver::_exec(const std::vector<Step>& p_steps, std::string* r_output)
{
    for (const Step& s : p_steps) {
        std::string out;
        const bool ok = _run(s, out);
        _log(s, out);
        if (r_output) *r_output += out;
        if (!ok) {
            const Problem p = _classify(out);
            _fail(p, _message(p, out));
            return false;
        }
    }
    return true;
}

void GitDriver::_fail(Problem p_problem, const std::string& p_error)
{
    std::lock_guard lock(mutex);
    status.problem = p_problem;
    status.error = p_error;
    status.version++;
}

void GitDriver::_finish(bool p_ok, bool p_report)
{
    if (!p_report) return;
    std::lock_guard lock(mutex);
    if (p_ok) {
        status.problem = Problem::NONE;
        status.error.clear();
    }
    status.failed = !p_ok;
    status.version++;
}

void GitDriver::_refresh_sync()
{
    Status s;
    std::string out;
    std::string signature;
    std::error_code ec;
    const bool folder = !repo_root.empty() && std::filesystem::is_directory(repo_root, ec);

    if (!_run({ { "--version" }, {} }, out)) {
        s.state = State::NO_GIT;
    } else {
        _run({ { "config", "--global", "user.name" }, {} }, s.user_name);
        _run({ { "config", "--global", "user.email" }, {} }, s.user_email);
        _trim(s.user_name);
        _trim(s.user_email);

        std::string top;
        bool is_repo = folder && _git_dir_exists() && _run({ { "rev-parse", "--show-toplevel" }, {} }, top);
        _trim(top);
        if (is_repo) is_repo = std::filesystem::equivalent(std::filesystem::path(top), repo_root, ec) && !ec;

        if (!is_repo) {
            s.state = State::NO_REPO;
        } else {
            _run({ { "branch", "--show-current" }, {} }, s.branch);
            _trim(s.branch);
            const bool has_head = _run({ { "rev-parse", "HEAD" }, {} }, s.head);
            _trim(s.head);
            if (!has_head) s.head.clear();
            s.detached = has_head && s.branch.empty();
            s.operation = _operation_detect();

            const bool has_remote = _run({ { "remote", "get-url", REMOTE }, {} }, s.remote_url);
            _trim(s.remote_url);
            if (!has_remote) s.remote_url.clear();
            s.state = has_remote ? State::READY : State::NO_REMOTE;

            out.clear();
            if (_run({ { "--no-optional-locks", "status", "--porcelain=v1", "-z" }, {} }, out)) s.changes = _parse_changes(out);

            out.clear();
            if (_run({ { "for-each-ref", "--format=%(refname)%1f%(objectname)", "refs/heads", "refs/remotes" }, {} }, out)) {
                for (const std::string& line : _split(out, '\n')) {
                    std::vector<std::string> f = _split(line, '\x1f');
                    if (f.size() < 2) continue;
                    _trim(f[1]);
                    const std::string heads = "refs/heads/";
                    const std::string remotes = "refs/remotes/";
                    if (f[0].starts_with(heads)) {
                        s.refs.push_back({ f[0].substr(heads.size()), f[1], false });
                    } else if (f[0].starts_with(remotes) && !f[0].ends_with("/HEAD")) {
                        s.refs.push_back({ f[0].substr(remotes.size()), f[1], true });
                    }
                }
            }

            if (has_remote) {
                out.clear();
                if (_run({ { "rev-list", "--branches", "--not", "--remotes" }, {} }, out)) {
                    for (std::string& h : _split(out, '\n')) {
                        _trim(h);
                        if (!h.empty()) s.unuploaded.push_back(h);
                    }
                }
                out.clear();
                if (_run({ { "rev-list", "--count", "--remotes", "--not", "--branches" }, {} }, out)) s.incoming = (int32_t)std::strtol(out.c_str(), nullptr, 10);
            }

            const std::string format = "--format=%H%x1f%P%x1f%an%x1f%ct%x1f%s%x1e";
            out.clear();
            bool logged = has_head && _run({ { "log", "--branches", "--remotes", "HEAD", "--date-order", format }, {} }, out);
            if (!logged && !s.refs.empty()) {
                out.clear();
                logged = _run({ { "log", "--branches", "--remotes", "--date-order", format }, {} }, out);
            }
            if (logged) {
                for (std::string& record : _split(out, '\x1e')) {
                    while (!record.empty() && (record.front() == '\n' || record.front() == '\r')) record.erase(0, 1);
                    std::vector<std::string> f = _split(record, '\x1f');
                    if (f.size() < 5 || f[0].empty()) continue;
                    Commit c;
                    c.hash = f[0];
                    if (!f[1].empty()) c.parents = _split(f[1], ' ');
                    c.author = f[2];
                    c.time = std::strtoll(f[3].c_str(), nullptr, 10);
                    c.summary = f[4];
                    s.commits.push_back(std::move(c));
                }
            }
            signature = _signature();
        }
    }

    std::lock_guard lock(mutex);
    s.failed = status.failed;
    s.problem = status.problem;
    s.error = status.error;
    if (!folder && s.state != State::NO_GIT) {
        s.problem = Problem::NO_FOLDER;
        s.error = _message(Problem::NO_FOLDER, {});
    } else if (s.problem == Problem::NO_FOLDER) {
        s.problem = Problem::NONE;
        s.error.clear();
    }
    s.version = status.version + 1;
    status = std::move(s);
    ref_signature = signature;
}

std::vector<std::string> GitDriver::_parse_changes(const std::string& p_porcelain)
{
    std::vector<std::string> changes;
    size_t i = 0;
    while (i < p_porcelain.size()) {
        const size_t end = p_porcelain.find('\0', i);
        const size_t stop = end == std::string::npos ? p_porcelain.size() : end;
        const std::string entry = p_porcelain.substr(i, stop - i);
        i = stop + 1;
        if (entry.size() < 4) continue;
        changes.push_back(entry);
        if (entry[0] == 'R' || entry[0] == 'C') {
            const size_t skip = p_porcelain.find('\0', i);
            i = skip == std::string::npos ? p_porcelain.size() : skip + 1;
        }
    }
    return changes;
}

GitDriver::Problem GitDriver::_classify(const std::string& p_output)
{
    const std::string l = _lower(p_output);
    auto has = [&](const char* p_text) { return l.find(p_text) != std::string::npos; };
    if (has("project folder no longer exists")) return Problem::NO_FOLDER;
    if (has("index.lock")) return Problem::LOCKED;
    if (has("repository not found") || has("does not appear to be a git repository") || has("remote: not found") || has("returned error: 404")) return Problem::REMOTE_MISSING;
    if (has("authentication failed") || has("could not read username") || has("permission denied") || has("returned error: 403") || has("access denied")) return Problem::AUTH;
    if (has("could not resolve host") || has("failed to connect") || has("timed out") || has("network is unreachable") || has("unable to access")) return Problem::OFFLINE;
    if (has("non-fast-forward") || has("fetch first") || has("[rejected]") || has("stale info")) return Problem::REJECTED;
    return Problem::FAILED;
}

std::string GitDriver::_message(Problem p_problem, const std::string& p_output)
{
    switch (p_problem) {
        case Problem::REMOTE_MISSING: return "The GitHub repository can't be found. It may have been deleted, renamed or made private. Change the URL or disconnect.";
        case Problem::BAD_URL: return "No repository was found at that URL. Check it and try again.";
        case Problem::UNRELATED: return "That GitHub repository already holds a different project. Use an empty repository or this project's own.";
        case Problem::AUTH: return "GitHub sign-in failed, or this account doesn't have access to the repository.";
        case Problem::OFFLINE: return "Can't reach GitHub. Check your internet connection and try again.";
        case Problem::REJECTED: return "GitHub has commits you don't have yet. Download first, then upload.";
        case Problem::LOCKED: return "Git is locked. Another git tool may be running, or a previous operation crashed.";
        case Problem::NO_FOLDER: return "The project folder no longer exists.";
        case Problem::FAILED: break;
        default: return "Something went wrong.";
    }
    std::string last;
    for (std::string& line : _split(p_output, '\n')) {
        _trim(line);
        if (!line.empty()) last = line;
    }
    return last.empty() ? std::string("Git failed.") : "Git failed: " + last;
}

void GitDriver::_log(const Step& p_step, const std::string& p_output)
{
    if (!log_callback) return;
    std::string cmd = "git";
    for (const std::string& a : p_step.args) cmd += " " + a;
    log_callback(cmd);
    for (std::string& line : _split(p_output, '\n')) {
        _trim(line);
        if (!line.empty()) log_callback("    " + line);
    }
}

bool GitDriver::_run(const Step& p_step, std::string& r_output)
{
    std::error_code ec;
    const bool version_only = p_step.args.size() == 1 && p_step.args[0] == "--version";
    if (!version_only && !std::filesystem::is_directory(repo_root, ec)) {
        r_output += "The project folder no longer exists.\n";
        return false;
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE in_read = nullptr;
    HANDLE in_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &sa, 0)) return false;
    if (!CreatePipe(&in_read, &in_write, &sa, 0)) {
        CloseHandle(out_read);
        CloseHandle(out_write);
        return false;
    }
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"git";
    for (const std::string& a : p_step.args) _append_quoted(cmd, _widen(a));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = out_write;
    PROCESS_INFORMATION pi{};
    const std::wstring cwd = repo_root.wstring();
    const BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, version_only ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(out_write);
    CloseHandle(in_read);
    if (!created) {
        CloseHandle(out_read);
        CloseHandle(in_write);
        r_output += "git was not found. Install Git for Windows and restart the editor.\n";
        return false;
    }

    if (!p_step.input.empty()) {
        DWORD written = 0;
        WriteFile(in_write, p_step.input.data(), (DWORD)p_step.input.size(), &written, nullptr);
    }
    CloseHandle(in_write);

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(out_read, buffer, (DWORD)sizeof(buffer), &read, nullptr) && read) r_output.append(buffer, read);
    CloseHandle(out_read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

void GitDriver::_join()
{
    if (worker.joinable()) worker.join();
    if (poll_worker.joinable()) poll_worker.join();
}

}