#ifdef WD_LINUX
#include "platform.hpp"
#include <spawn.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <algorithm>

extern char** environ;

namespace watchdog {

struct ProcessHandle {
    pid_t    pid          = 0;
    uint64_t prev_utime   = 0;   // from /proc/PID/stat field 14 (clock ticks)
    uint64_t prev_stime   = 0;   // from /proc/PID/stat field 15 (clock ticks)
    uint64_t prev_wall_ns = 0;   // wall clock at last sample (nanoseconds)
    bool     first_sample = true;
};

// Get monotonic time in nanoseconds
static uint64_t mono_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

ProcessHandle* spawn_process(int argc, char** argv) {
    if (argc <= 0 || !argv || !argv[0]) {
        std::cerr << "[watchdog] spawn_process: empty argv\n";
        return nullptr;
    }

    // Build null-terminated argv (already null-terminated by caller, but be safe)
    // posix_spawnp expects a null-terminated array
    pid_t pid = 0;
    int ret = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ);
    if (ret != 0) {
        std::cerr << "[watchdog] posix_spawnp failed: " << std::strerror(ret) << "\n";
        return nullptr;
    }

    ProcessHandle* h = new ProcessHandle();
    h->pid           = pid;
    h->prev_wall_ns  = mono_ns();
    return h;
}

ProcessStats sample_stats(ProcessHandle* h) {
    ProcessStats stats;

    // Check if process is alive by trying to open /proc/PID/stat
    std::string stat_path = "/proc/" + std::to_string(h->pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        stats.alive = false;
        return stats;
    }

    // /proc/PID/stat is a single line; fields are space-separated.
    // Field 14 = utime (index 13, 0-based), field 15 = stime (index 14)
    // but the process name (field 2) can contain spaces and is wrapped in ()
    // So we skip past the closing ')' before parsing fields 3+
    std::string line;
    std::getline(stat_file, line);
    stat_file.close();

    // Find the last ')' — the process name ends there
    auto rparen = line.rfind(')');
    if (rparen == std::string::npos) {
        stats.alive = false;
        return stats;
    }

    // Fields after ')' start at rparen+2 (skip ') ')
    // Fields: state(0) ppid(1) pgrp(2) session(3) tty(4) tpgid(5)
    //         flags(6) minflt(7) cminflt(8) majflt(9) cmajflt(10)
    //         utime(11) stime(12) ...
    // utime is field index 11 from start of post-paren string (0-based)
    std::istringstream ss(line.substr(rparen + 2));
    std::string token;
    uint64_t utime = 0, stime = 0;

    for (int i = 0; ss >> token; ++i) {
        if (i == 11) utime = std::stoull(token);
        if (i == 12) { stime = std::stoull(token); break; }
    }

    uint64_t now_ns   = mono_ns();
    uint64_t total_ticks = utime + stime;
    long clk_tck      = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    if (h->first_sample) {
        h->first_sample  = false;
        h->prev_utime    = utime;
        h->prev_stime    = stime;
        h->prev_wall_ns  = now_ns;
        stats.cpu_percent = 0.0;
    } else {
        uint64_t delta_ticks = (utime + stime)
                             - (h->prev_utime + h->prev_stime);
        uint64_t delta_wall_ns = now_ns - h->prev_wall_ns;

        if (delta_wall_ns > 0) {
            double delta_cpu_sec  = static_cast<double>(delta_ticks)
                                  / static_cast<double>(clk_tck);
            double delta_wall_sec = static_cast<double>(delta_wall_ns) / 1e9;
            double cpu_pct        = (delta_cpu_sec / delta_wall_sec) * 100.0;

            // Clamp to [0, 100 * num_cpus]
            long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
            if (num_cpus <= 0) num_cpus = 1;
            double max_pct = 100.0 * static_cast<double>(num_cpus);
            if (cpu_pct < 0.0)      cpu_pct = 0.0;
            if (cpu_pct > max_pct)  cpu_pct = max_pct;

            stats.cpu_percent = cpu_pct;
        }

        h->prev_utime   = utime;
        h->prev_stime   = stime;
        h->prev_wall_ns = now_ns;
    }

    // Read RSS from /proc/PID/status — line "VmRSS: <kB>"
    std::string status_path = "/proc/" + std::to_string(h->pid) + "/status";
    std::ifstream status_file(status_path);
    if (status_file.is_open()) {
        std::string sline;
        while (std::getline(status_file, sline)) {
            if (sline.compare(0, 6, "VmRSS:") == 0) {
                std::istringstream rss_ss(sline.substr(6));
                uint64_t rss_kb = 0;
                rss_ss >> rss_kb;
                stats.rss_bytes = rss_kb * 1024ULL;
                break;
            }
        }
    }

    (void)total_ticks; // suppress unused warning
    stats.alive = true;
    return stats;
}

void kill_process(ProcessHandle* h) {
    if (h && h->pid > 0) {
        kill(h->pid, SIGKILL);
    }
}

int wait_process(ProcessHandle* h) {
    if (!h || h->pid <= 0) return -1;

    int status = 0;
    waitpid(h->pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

void close_handle(ProcessHandle* h) {
    delete h;
}

void suspend_process(ProcessHandle* h) {
    if (h && h->pid > 0) {
        kill(h->pid, SIGSTOP);
    }
}

void resume_process(ProcessHandle* h) {
    if (h && h->pid > 0) {
        kill(h->pid, SIGCONT);
    }
}

} // namespace watchdog
#endif // WD_LINUX
