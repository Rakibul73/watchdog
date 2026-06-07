#include "cli.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace watchdog {

// Convert a string to lowercase in-place
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Parse a memory string like "512mb", "1gb", "1024kb", or raw integer bytes.
// Returns bytes on success, throws std::runtime_error on failure.
static uint64_t parse_mem(const std::string& raw) {
    if (raw.empty()) {
        throw std::runtime_error("empty memory value");
    }

    std::string lower = to_lower(raw);

    // Try suffix matching
    struct Suffix { const char* suffix; uint64_t multiplier; };
    static const Suffix suffixes[] = {
        {"gb",  1024ULL * 1024 * 1024},
        {"mb",  1024ULL * 1024},
        {"kb",  1024ULL},
        {"g",   1024ULL * 1024 * 1024},
        {"m",   1024ULL * 1024},
        {"k",   1024ULL},
        {"b",   1ULL},
    };

    for (const auto& s : suffixes) {
        std::string sfx(s.suffix);
        if (lower.size() > sfx.size() &&
            lower.compare(lower.size() - sfx.size(), sfx.size(), sfx) == 0)
        {
            std::string num_part = raw.substr(0, raw.size() - sfx.size());
            // trim whitespace
            auto start = num_part.find_first_not_of(" \t");
            auto end   = num_part.find_last_not_of(" \t");
            if (start == std::string::npos) {
                throw std::runtime_error("invalid memory value: '" + raw + "'");
            }
            num_part = num_part.substr(start, end - start + 1);
            try {
                uint64_t val = std::stoull(num_part);
                return val * s.multiplier;
            } catch (...) {
                throw std::runtime_error("invalid memory value: '" + raw + "'");
            }
        }
    }

    // No suffix — treat as raw bytes
    try {
        return std::stoull(raw);
    } catch (...) {
        throw std::runtime_error("invalid memory value: '" + raw + "'");
    }
}

// Parse a CPU limit string. Accepts: "30", "30%", "0.3"
// Returns a percentage in [0.0, 100.0 * logical cores].
// Throws std::runtime_error on failure.
static double parse_cpu(const std::string& raw) {
    if (raw.empty()) {
        throw std::runtime_error("empty cpu value");
    }

    std::string s = raw;
    bool is_percent = false;

    if (!s.empty() && s.back() == '%') {
        is_percent = true;
        s.pop_back();
    }

    double val = 0.0;
    try {
        std::size_t pos = 0;
        val = std::stod(s, &pos);
        if (pos != s.size()) {
            throw std::runtime_error("invalid cpu value: '" + raw + "'");
        }
    } catch (const std::runtime_error&) {
        throw;
    } catch (...) {
        throw std::runtime_error("invalid cpu value: '" + raw + "'");
    }

    if (!is_percent && val > 0.0 && val <= 1.0) {
        // Treat as fraction (e.g. 0.3 -> 30%)
        val *= 100.0;
    }
    // Otherwise val is already a percentage (e.g. 30 -> 30%)

    return val;
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs result;

    if (argc <= 1) {
        result.show_help = true;
        return result;
    }

    // Find the "--" separator first
    int separator_idx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--") {
            separator_idx = i;
            break;
        }
    }

    // Parse watchdog flags
    int flag_end = (separator_idx >= 0) ? separator_idx : argc;

    for (int i = 1; i < flag_end; ++i) {
        std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            result.show_help = true;
            return result;
        }
        else if (arg == "--verbose" || arg == "-v") {
            result.cfg.verbose = true;
        }
        else if (arg == "--cpu") {
            if (i + 1 >= flag_end) {
                result.parse_error = true;
                result.error_msg   = "--cpu requires a value (e.g. --cpu 30%)";
                return result;
            }
            try {
                result.cfg.cpu_limit_pct = parse_cpu(argv[++i]);
            } catch (const std::exception& e) {
                result.parse_error = true;
                result.error_msg   = e.what();
                return result;
            }
            if (result.cfg.cpu_limit_pct <= 0.0) {
                result.parse_error = true;
                result.error_msg   = "cpu limit must be > 0";
                return result;
            }
        }
        else if (arg == "--mem") {
            if (i + 1 >= flag_end) {
                result.parse_error = true;
                result.error_msg   = "--mem requires a value (e.g. --mem 512mb)";
                return result;
            }
            try {
                result.cfg.mem_limit_bytes = parse_mem(argv[++i]);
            } catch (const std::exception& e) {
                result.parse_error = true;
                result.error_msg   = e.what();
                return result;
            }
            // --mem 0 means no memory limit (valid, treat as disabled)
        }
        else if (arg == "--poll") {
            if (i + 1 >= flag_end) {
                result.parse_error = true;
                result.error_msg   = "--poll requires a value in milliseconds";
                return result;
            }
            try {
                long ms = std::stol(argv[++i]);
                if (ms <= 0) {
                    result.parse_error = true;
                    result.error_msg   = "--poll value must be a positive integer";
                    return result;
                }
                result.cfg.poll_ms = static_cast<uint32_t>(ms);
            } catch (...) {
                result.parse_error = true;
                result.error_msg   = "--poll value is not a valid integer";
                return result;
            }
            if (result.cfg.poll_ms < 50) {
                std::cerr << "[watchdog] warning: poll interval below 50ms may cause high watchdog overhead\n";
            }
        }
        else if (!arg.empty() && arg[0] == '-') {
            // Unknown flag
            result.parse_error = true;
            result.error_msg   = "unknown option: " + arg;
            return result;
        }
        else {
            // Bare argument without "--" separator — treat remaining as child command
            for (int j = i; j < flag_end; ++j) {
                result.child_argv.push_back(argv[j]);
            }
            return result;
        }
    }

    // Collect child argv after "--"
    if (separator_idx >= 0) {
        for (int i = separator_idx + 1; i < argc; ++i) {
            result.child_argv.push_back(argv[i]);
        }
    }

    return result;
}

void print_usage(const char* prog) {
    std::cerr
        << "\nUsage: " << prog << " [options] -- <command> [args...]\n"
        << "\nOptions:\n"
        << "  --cpu <limit>    CPU cap per core: 30, 30%, or 0.3 (fraction)\n"
        << "  --mem <limit>    RAM cap: 512mb, 1gb, 256kb, or raw bytes\n"
        << "  --poll <ms>      Sampling interval in milliseconds (default: 200)\n"
        << "  --verbose, -v    Print live CPU/RAM stats each poll interval\n"
        << "  --help, -h       Show this help message\n"
        << "\nExamples:\n"
        << "  " << prog << " --cpu 30% --mem 512mb -- cargo build\n"
        << "  " << prog << " --cpu 50% --verbose   -- npm install\n"
        << "  " << prog << " --mem 1gb             -- python train.py\n"
        << "  " << prog << " --cpu 25% --mem 256mb -- make -j8\n"
        << "\nNotes:\n"
        << "  CPU throttling uses a duty-cycle (SIGSTOP/SIGCONT or SuspendThread).\n"
        << "  Memory enforcement is hard: process is killed when RSS exceeds the limit.\n"
        << "\n";
}

} // namespace watchdog
