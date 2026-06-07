#pragma once
#include "platform.hpp"
#include <cstdint>

namespace watchdog {

struct ThrottleConfig {
    double   cpu_limit_pct   = 100.0;  // 0 = no limit
    uint64_t mem_limit_bytes = 0;      // 0 = no limit
    uint32_t poll_ms         = 200;    // sampling interval in ms
    bool     verbose         = false;
};

// Runs throttle loop in the calling thread until child exits.
// Returns child exit code.
int run_throttle_loop(ProcessHandle* h, const ThrottleConfig& cfg);

} // namespace watchdog
