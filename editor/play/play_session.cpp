#include <editor/play/play_session.h>
#include <windows.h>
#include <string>
#include <vector>

namespace lumen {

Error PlaySession::initialize()
{
    using enum Error;

    HANDLE j = CreateJobObjectW(nullptr, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(!j, FAILED, "PlaySession: CreateJobObject failed.");

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (!SetInformationJobObject(j, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        log_write("PlaySession: SetInformationJobObject failed (%lu).", GetLastError());
        CloseHandle(j);
        return FAILED;
    }

    job = j;
    return OK;
}

void PlaySession::shutdown()
{
    if (running) TerminateProcess((HANDLE)process, 0);
    _close_handles();
    running = false;
    stopping = false;

    if (job) {
        CloseHandle((HANDLE)job);
        job = nullptr;
    }
}

Error PlaySession::launch(const std::filesystem::path& p_project_root)
{
    using enum Error;
    if (running) return OK;
    LUMEN_ERR_FAIL_COND_V_MSG(p_project_root.empty(), FAILED, "PlaySession: no project loaded.");

    wchar_t exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    LUMEN_ERR_FAIL_COND_V_MSG(n == 0 || n == MAX_PATH, FAILED, "PlaySession: GetModuleFileName failed.");

    const std::filesystem::path v_root = _normalize_root(p_project_root);

    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" --game --pie --project \"";
    cmd += v_root.wstring();
    cmd += L"\"";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        log_write("PlaySession: CreateProcess failed (%lu).", GetLastError());
        return FAILED;
    }

    if (job && !AssignProcessToJobObject((HANDLE)job, pi.hProcess)) log_write("PlaySession: AssignProcessToJobObject failed (%lu); game will not close with the editor.", GetLastError());

    AllowSetForegroundWindow(pi.dwProcessId);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    process = pi.hProcess;
    process_id = pi.dwProcessId;
    this->root = v_root;
    running = true;
    stopping = false;
    terminated = false;
    stop_deadline_ms = 0;

    log_write("Play: launched game (pid %lu) for '%s'.", pi.dwProcessId, root.string().c_str());
    return OK;
}

void PlaySession::relaunch(const std::filesystem::path& p_project_root)
{
    if (!running) {
        launch(p_project_root);
        return;
    }
    pending_root = p_project_root;
    stop();
}

bool PlaySession::is_running_for(const std::filesystem::path& p_project_root) const
{
    return running && root == _normalize_root(p_project_root);
}

std::filesystem::path PlaySession::_normalize_root(const std::filesystem::path& p_project_root)
{
    std::filesystem::path r = p_project_root.lexically_normal();
    if (!r.has_filename()) r = r.parent_path();
    return r;
}

static BOOL CALLBACK _close_process_windows(HWND p_hwnd, LPARAM p_pid)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(p_hwnd, &pid);
    if (pid == (DWORD)p_pid) PostMessageW(p_hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

void PlaySession::stop()
{
    if (!running) return;

    if (stopping) {
        terminated = true;
        TerminateProcess((HANDLE)process, 0);
        return;
    }

    EnumWindows(_close_process_windows, (LPARAM)process_id);
    stopping = true;
    stop_deadline_ms = GetTickCount64() + STOP_TIMEOUT_MS;
}

void PlaySession::poll()
{
    if (!running) {
        if (!pending_root.empty()) {
            const std::filesystem::path next = pending_root;
            pending_root.clear();
            launch(next);
        }
        return;
    }

    if (WaitForSingleObject((HANDLE)process, 0) == WAIT_OBJECT_0) {
        _on_exit();
        return;
    }

    if (stopping && !terminated && GetTickCount64() >= stop_deadline_ms) {
        log_write("Play: game did not close within %u ms, terminating.", STOP_TIMEOUT_MS);
        terminated = true;
        TerminateProcess((HANDLE)process, 0);
    }
}

void PlaySession::_on_exit()
{
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)process, &code);

    last_exit_code = code;
    last_crashed = !terminated && code != 0;

    if (terminated) log_write("Play: game terminated.");
    else if (code == 0) log_write("Play: game exited.");
    else if (code >= 0xC0000000u) log_write("Play: game crashed (exception 0x%08lX). Editor unaffected.", code);
    else log_write("Play: game exited with error code %lu (see its crash log). Editor unaffected.", code);

    _close_handles();
    root.clear();
    running = false;
    stopping = false;
    terminated = false;
}

void PlaySession::_close_handles()
{
    if (process) {
        CloseHandle((HANDLE)process);
        process = nullptr;
    }
    process_id = 0;
}

}
