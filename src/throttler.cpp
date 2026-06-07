#include "throttler.hpp"
#include "platform.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <cstdint>

namespace watchdog {

// Format elapsed seconds as HH:MM:SS
static std::string format_elapsed(uint64_t total_seconds) {
    unsigned hh = static_cast<unsigned>(total_seconds / 3600);
    unsigned mm = static_cast<unsigned>((total_seconds % 3600) / 60);
    unsigned ss = static_cast<unsigned>(total_seconds % 60);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hh, mm, ss);
    return std::string(buf);
}

// Format bytes as human-readable MB string
static std::string format_mb(uint64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    return std::string(buf);
}

int run_throttle_loop(ProcessHandle* h, const ThrottleConfig& cfg) {
    using namespace std::chrono;

    const bool   has_cpu_limit = (cfg.cpu_limit_pct < 100.0);
    const bool   has_mem_limit = (cfg.mem_limit_bytes > 0);
    const double poll_ms_d     = static_cast<double>(cfg.poll_ms);

    auto start_time = steady_clock::now();

    // First sample to initialise internal counters — always returns cpu=0
    {
        ProcessStats first = sample_stats(h);
        if (!first.alive) {
            return wait_process(h);
        }
    }

    while (true) {
        // Always resume before the "run" slice so the child is never left frozen
        resume_process(h);

        // Run slice
        double run_ms = has_cpu_limit
            ? (poll_ms_d * cfg.cpu_limit_pct / 100.0)
            : poll_ms_d;

        std::this_thread::sleep_for(
            duration_cast<nanoseconds>(duration<double, std::milli>(run_ms))
        );

        // Sample after the run slice
        ProcessStats stats = sample_stats(h);

        // Check if child already exited
        if (!stats.alive) {
            // Make sure process isn't suspended before we wait
            resume_process(h);
            return wait_process(h);
        }

        // Memory enforcement
        if (has_mem_limit && stats.rss_bytes > cfg.mem_limit_bytes) {
            double rss_mb   = static_cast<double>(stats.rss_bytes)   / (1024.0 * 1024.0);
            double limit_mb = static_cast<double>(cfg.mem_limit_bytes) / (1024.0 * 1024.0);
            std::cerr << "\n[watchdog] killed: RSS "
                      << std::fixed << std::setprecision(0)
                      << rss_mb << " MB exceeded limit "
                      << limit_mb << " MB\n";
            kill_process(h);
            return wait_process(h);
        }

        // Verbose output — overwrite same line
        if (cfg.verbose) {
            auto now     = steady_clock::now();
            uint64_t sec = static_cast<uint64_t>(
                duration_cast<seconds>(now - start_time).count()
            );

            std::cerr << "\r[watchdog] cpu: "
                      << std::fixed << std::setprecision(1)
                      << std::setw(5) << stats.cpu_percent << "%"
                      << "  mem: " << format_mb(stats.rss_bytes);

            if (has_mem_limit) {
                std::cerr << " / " << format_mb(cfg.mem_limit_bytes);
            }

            std::cerr << "  elapsed: " << format_elapsed(sec)
                      << "    " // trailing spaces to overwrite longer previous lines
                      << std::flush;
        }

        // CPU throttling: if usage exceeds target (with 10% headroom), apply sleep slice
        if (has_cpu_limit && stats.cpu_percent > cfg.cpu_limit_pct * 1.1) {
            suspend_process(h);

            double sleep_ms = poll_ms_d * (1.0 - cfg.cpu_limit_pct / 100.0);
            if (sleep_ms > 0.0) {
                std::this_thread::sleep_for(
                    duration_cast<nanoseconds>(duration<double, std::milli>(sleep_ms))
                );
            }

            resume_process(h);
        }
    }
}

} // namespace watchdog
