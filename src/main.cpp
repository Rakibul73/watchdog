#include "cli.hpp"
#include "platform.hpp"
#include "throttler.hpp"
#include <iostream>
#include <vector>
#include <cstring>

int main(int argc, char** argv) {
    using namespace watchdog;

    CliArgs args = parse_args(argc, argv);

    if (args.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (args.parse_error) {
        std::cerr << "[watchdog] error: " << args.error_msg << "\n";
        print_usage(argv[0]);
        return 1;
    }

    if (args.child_argv.empty()) {
        std::cerr << "[watchdog] error: no command specified after --\n";
        print_usage(argv[0]);
        return 1;
    }

    // Build a null-terminated argv array for the child
    std::vector<char*> child_argv_ptrs;
    child_argv_ptrs.reserve(args.child_argv.size() + 1);
    for (auto& s : args.child_argv)
        child_argv_ptrs.push_back(const_cast<char*>(s.c_str()));
    child_argv_ptrs.push_back(nullptr);

    ProcessHandle* h = spawn_process(
        static_cast<int>(child_argv_ptrs.size()) - 1,
        child_argv_ptrs.data()
    );

    if (!h) {
        std::cerr << "[watchdog] failed to spawn process\n";
        return 1;
    }

    if (args.cfg.verbose) {
        std::cerr << "[watchdog] started: " << args.child_argv[0] << "\n";
        if (args.cfg.cpu_limit_pct < 100.0)
            std::cerr << "[watchdog] cpu limit: " << args.cfg.cpu_limit_pct << "%\n";
        if (args.cfg.mem_limit_bytes > 0)
            std::cerr << "[watchdog] mem limit: "
                      << (args.cfg.mem_limit_bytes / (1024ULL * 1024ULL)) << " MB\n";
    }

    int exit_code = run_throttle_loop(h, args.cfg);

    if (args.cfg.verbose)
        std::cerr << "\n[watchdog] child exited with code " << exit_code << "\n";

    close_handle(h);
    return exit_code;
}
