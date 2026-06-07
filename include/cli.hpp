#pragma once
#include "throttler.hpp"
#include <string>
#include <vector>

namespace watchdog {

struct CliArgs {
    ThrottleConfig           cfg;
    std::vector<std::string> child_argv;
    bool                     show_help   = false;
    bool                     parse_error = false;
    std::string              error_msg;
};

CliArgs parse_args(int argc, char** argv);
void    print_usage(const char* prog);

} // namespace watchdog
