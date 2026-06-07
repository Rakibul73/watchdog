# watchdog

> Cap CPU and RAM of any process. No Docker. No root. One binary.

## The problem

`npm install` eats 100% CPU and freezes your laptop.  
`cargo build` exhausts your RAM mid-demo.  
You have no portable way to say: *run this, but only use 30% CPU and 512 MB RAM*.

`watchdog` fixes that.

## Usage

    watchdog [options] -- <command> [args...]

    Options:
      --cpu <limit>   CPU cap: 30, 30%, 0.3 (fraction of one core)
      --mem <limit>   RAM cap: 512mb, 1gb, 256kb, or raw bytes
      --poll <ms>     Sampling interval in ms (default: 200)
      --verbose, -v   Print live CPU/RAM stats
      --help, -h      Show this message

## Examples

    watchdog --cpu 30% --mem 512mb -- cargo build
    watchdog --cpu 50% --verbose   -- npm install
    watchdog --mem 1gb             -- python train.py
    watchdog --cpu 25% --mem 256mb -- make -j8

## How it works

| Platform | CPU throttling         | Memory enforcement    |
|----------|------------------------|-----------------------|
| Linux    | SIGSTOP / SIGCONT      | /proc/PID/status      |
| Windows  | SuspendThread / Resume | GetProcessMemoryInfo  |
| macOS    | SIGSTOP / SIGCONT      | proc_pidinfo          |

CPU throttling uses a **duty-cycle** approach: during each polling window,
the process runs for `(limit%)` of the window and is suspended for the rest.

Memory enforcement is hard: when RSS exceeds the limit the process is killed
immediately with a clear error message.

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./build/watchdog --help

Requires: C++17 compiler, CMake 3.16+. No other dependencies.

## Platform support

- Linux (kernel 3.2+, glibc 2.17+)
- Windows 10+ (MSVC 2019+ or MinGW)
- macOS 10.15+

## Verbose output format

    [watchdog] cpu:  28.4%  mem: 214.0 MB / 512.0 MB  elapsed: 00:01:23

The line is overwritten in-place using `\r` so your terminal stays clean.

## Notes

- `--cpu 100%` with no `--mem`: monitoring only, no throttling applied
- `--cpu 0%`: rejected with an error
- `--mem 0`: treated as no memory limit
- If the child exits before the first sample, its exit code is returned cleanly
- `--poll` values under 50ms are allowed but trigger a warning about overhead
