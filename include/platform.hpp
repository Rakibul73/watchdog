#pragma once

#include <cstdint>
#include <string>

namespace watchdog {

struct ProcessStats {
    double   cpu_percent = 0.0;   // 0.0–100.0 (per logical core)
    uint64_t rss_bytes   = 0;     // resident set size in bytes
    bool     alive       = true;
};

struct ProcessHandle;

// Spawn child process — argv[0] is searched on PATH.
ProcessHandle* spawn_process(int argc, char** argv);

// Sample CPU% (delta since last call) and RSS.
// Call at fixed intervals; first call always returns cpu=0.
ProcessStats   sample_stats(ProcessHandle* h);

// Hard kill (SIGKILL / TerminateProcess).
void           kill_process(ProcessHandle* h);

// Wait for exit, return exit code.
int            wait_process(ProcessHandle* h);

// Release OS resources.
void           close_handle(ProcessHandle* h);

// Freeze / unfreeze all threads (used for CPU duty-cycle throttling).
void           suspend_process(ProcessHandle* h);
void           resume_process(ProcessHandle* h);

} // namespace watchdog
