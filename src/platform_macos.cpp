#include "platform.hpp"
#include <libproc.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <iostream>

extern char** environ;

namespace watchdog {

struct ProcessHandle {
    pid_t    pid            = 0;
    uint64_t prev_user_ns   = 0;   // pti_total_user (nanoseconds)
    uint64_t prev_system_ns = 0;   // pti_total_system (nanoseconds)
    uint64_t prev_wall_ns   = 0;   // wall clock at last sample (nanoseconds)
    bool     first_sample   = true;
};

// Monotonic time in nanoseconds
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

    if (!h || h->pid <= 0) {
        stats.alive = false;
        return stats;
    }

    struct proc_taskinfo pti = {};
    int ret = proc_pidinfo(h->pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
    if (ret <= 0) {
        // Process is gone
        stats.alive = false;
        return stats;
    }

    uint64_t now_ns      = mono_ns();
    uint64_t user_ns     = pti.pti_total_user;
    uint64_t system_ns   = pti.pti_total_system;

    if (h->first_sample) {
        h->first_sample   = false;
        h->prev_user_ns   = user_ns;
        h->prev_system_ns = system_ns;
        h->prev_wall_ns   = now_ns;
        stats.cpu_percent = 0.0;
    } else {
        uint64_t delta_cpu_ns  = (user_ns + system_ns)
                               - (h->prev_user_ns + h->prev_system_ns);
        uint64_t delta_wall_ns = now_ns - h->prev_wall_ns;

        if (delta_wall_ns > 0) {
            double cpu_pct = static_cast<double>(delta_cpu_ns)
                           / static_cast<double>(delta_wall_ns)
                           * 100.0;

            // Clamp to [0, 100 * num_cpus]
            long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
            if (num_cpus <= 0) num_cpus = 1;
            double max_pct = 100.0 * static_cast<double>(num_cpus);
            if (cpu_pct < 0.0)     cpu_pct = 0.0;
            if (cpu_pct > max_pct) cpu_pct = max_pct;

            stats.cpu_percent = cpu_pct;
        }

        h->prev_user_ns   = user_ns;
        h->prev_system_ns = system_ns;
        h->prev_wall_ns   = now_ns;
    }

    stats.rss_bytes = static_cast<uint64_t>(pti.pti_resident_size);
    stats.alive     = true;
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

