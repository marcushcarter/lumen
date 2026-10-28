#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#ifndef LUMEN_WRAPPER_TARGET
#error "lumen_console_wrapper.cpp requires LUMEN_WRAPPER_TARGET (e.g. L\"lumen_editor_app.exe\")"
#endif

static const wchar_t* skip_first_arg(const wchar_t* cmd)
{
    bool in_quotes = false;
    while (*cmd) {
        const wchar_t c = *cmd;
        if (c == L'"') in_quotes = !in_quotes;
        else if (!in_quotes && (c == L' ' || c == L'\t')) break;
        ++cmd;
    }
    while (*cmd == L' ' || *cmd == L'\t') ++cmd;
    return cmd;
}

int main()
{
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    std::wstring dir(exe_path);
    const size_t slash = dir.find_last_of(L"\\/");
    const std::wstring target = dir.substr(0, slash + 1) + LUMEN_WRAPPER_TARGET;

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        wprintf(L"CreateJobObject failed: %lu\n", GetLastError());
        return -1;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

    std::wstring cmd_line = L"\"" + target + L"\"";
    const wchar_t* forwarded = skip_first_arg(GetCommandLineW());
    if (*forwarded) {
        cmd_line += L" ";
        cmd_line += forwarded;
    }
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(target.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        wprintf(L"CreateProcess failed: %lu\n", GetLastError());
        CloseHandle(job);
        return -1;
    }

    AllowSetForegroundWindow(pi.dwProcessId);
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return static_cast<int>(exit_code);
}
