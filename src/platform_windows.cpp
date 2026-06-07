#include "platform.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <cstdint>

#pragma comment(lib, "psapi.lib")

namespace watchdog {

struct ProcessHandle {
    HANDLE         hProcess         = INVALID_HANDLE_VALUE;
    HANDLE         hThread          = INVALID_HANDLE_VALUE;
    DWORD          pid              = 0;
    ULARGE_INTEGER prev_kernel_time = {};
    ULARGE_INTEGER prev_user_time   = {};
    ULARGE_INTEGER prev_wall_time   = {};
    bool           first_sample     = true;
};

// Return current wall-clock time as 100-ns intervals (FILETIME epoch)
static ULARGE_INTEGER get_wall_time() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli;
}

static std::string get_last_error_str() {
    DWORD err = GetLastError();
    if (err == 0) return "unknown error";

    LPSTR buf = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr
    );

    std::string msg;
    if (size > 0 && buf) {
        msg = std::string(buf, size);
        // Strip trailing newline/whitespace
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' '))
            msg.pop_back();
    } else {
        msg = "error code " + std::to_string(err);
    }

    if (buf) LocalFree(buf);
    return msg;
}

ProcessHandle* spawn_process(int argc, char** argv) {
    if (argc <= 0 || !argv || !argv[0]) {
        std::cerr << "[watchdog] spawn_process: empty argv\n";
        return nullptr;
    }

    // Build command line string — quote each argument
    std::string cmdline;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) cmdline += ' ';
        std::string arg(argv[i]);
        // Simple quoting: wrap in double quotes if it contains spaces
        bool needs_quote = (arg.find(' ') != std::string::npos ||
                            arg.find('\t') != std::string::npos ||
                            arg.empty());
        if (needs_quote) {
            cmdline += '"';
            // Escape backslashes and double-quotes inside
            for (char c : arg) {
                if (c == '"') cmdline += '\\';
                cmdline += c;
            }
            cmdline += '"';
        } else {
            cmdline += arg;
        }
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi = {};

    // CREATE_SUSPENDED so we control from the start; resume immediately after
    BOOL ok = CreateProcessA(
        nullptr,
        const_cast<LPSTR>(cmdline.c_str()),
        nullptr,   // process security
        nullptr,   // thread security
        TRUE,      // inherit handles
        CREATE_SUSPENDED,
        nullptr,   // environment (inherit)
        nullptr,   // current directory (inherit)
        &si, &pi
    );

    if (!ok) {
        std::cerr << "[watchdog] CreateProcess failed: " << get_last_error_str() << "\n";
        return nullptr;
    }

    // Resume the main thread immediately
    ResumeThread(pi.hThread);

    ProcessHandle* h    = new ProcessHandle();
    h->hProcess         = pi.hProcess;
    h->hThread          = pi.hThread;
    h->pid              = pi.dwProcessId;
    h->prev_wall_time   = get_wall_time();
    return h;
}

ProcessStats sample_stats(ProcessHandle* h) {
    ProcessStats stats;

    if (!h || h->hProcess == INVALID_HANDLE_VALUE) {
        stats.alive = false;
        return stats;
    }

    // Check if process has exited
    DWORD wait_result = WaitForSingleObject(h->hProcess, 0);
    if (wait_result == WAIT_OBJECT_0) {
        stats.alive = false;
        return stats;
    }

    // CPU times
    FILETIME ft_creation, ft_exit, ft_kernel, ft_user;
    if (!GetProcessTimes(h->hProcess, &ft_creation, &ft_exit, &ft_kernel, &ft_user)) {
        stats.alive = false;
        return stats;
    }

    ULARGE_INTEGER kernel_time, user_time;
    kernel_time.LowPart  = ft_kernel.dwLowDateTime;
    kernel_time.HighPart = ft_kernel.dwHighDateTime;
    user_time.LowPart    = ft_user.dwLowDateTime;
    user_time.HighPart   = ft_user.dwHighDateTime;

    ULARGE_INTEGER wall_time = get_wall_time();

    if (h->first_sample) {
        h->first_sample       = false;
        h->prev_kernel_time   = kernel_time;
        h->prev_user_time     = user_time;
        h->prev_wall_time     = wall_time;
        stats.cpu_percent     = 0.0;
    } else {
        // All FILETIME values are in 100-nanosecond intervals
        ULONGLONG delta_kernel = kernel_time.QuadPart - h->prev_kernel_time.QuadPart;
        ULONGLONG delta_user   = user_time.QuadPart   - h->prev_user_time.QuadPart;
        ULONGLONG delta_wall   = wall_time.QuadPart   - h->prev_wall_time.QuadPart;

        if (delta_wall > 0) {
            double cpu_pct = static_cast<double>(delta_kernel + delta_user)
                           / static_cast<double>(delta_wall)
                           * 100.0;

            // Clamp
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            double max_pct = 100.0 * static_cast<double>(si.dwNumberOfProcessors);
            if (cpu_pct < 0.0)     cpu_pct = 0.0;
            if (cpu_pct > max_pct) cpu_pct = max_pct;

            stats.cpu_percent = cpu_pct;
        }

        h->prev_kernel_time = kernel_time;
        h->prev_user_time   = user_time;
        h->prev_wall_time   = wall_time;
    }

    // RSS via WorkingSetSize
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h->hProcess, &pmc, sizeof(pmc))) {
        stats.rss_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);
    }

    stats.alive = true;
    return stats;
}

void kill_process(ProcessHandle* h) {
    if (h && h->hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(h->hProcess, 1);
    }
}

int wait_process(ProcessHandle* h) {
    if (!h || h->hProcess == INVALID_HANDLE_VALUE) return -1;

    WaitForSingleObject(h->hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(h->hProcess, &exit_code);
    return static_cast<int>(exit_code);
}

void close_handle(ProcessHandle* h) {
    if (!h) return;
    if (h->hProcess != INVALID_HANDLE_VALUE) CloseHandle(h->hProcess);
    if (h->hThread  != INVALID_HANDLE_VALUE) CloseHandle(h->hThread);
    delete h;
}

void suspend_process(ProcessHandle* h) {
    if (!h || h->pid == 0) return;

    // Enumerate all threads belonging to this process and suspend each
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == h->pid) {
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (ht != nullptr) {
                    SuspendThread(ht);
                    CloseHandle(ht);
                }
            }
        } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
}

void resume_process(ProcessHandle* h) {
    if (!h || h->pid == 0) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == h->pid) {
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (ht != nullptr) {
                    // ResumeThread decrements suspend count; call until unsuspended
                    DWORD result;
                    do {
                        result = ResumeThread(ht);
                    } while (result > 1);
                    CloseHandle(ht);
                }
            }
        } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
}

} // namespace watchdog

