# watchdog

> Cap CPU and RAM of any process. No Docker. No root. One binary.

## The problem

`npm install` eats 100% CPU and freezes your laptop.  
`cargo build` exhausts your RAM mid-demo.  
You have no portable way to say: *run this, but only use 30% CPU and 512 MB RAM*.

`watchdog` fixes that.

---

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

---

## How it works

| Platform | CPU throttling         | Memory enforcement    |
|----------|------------------------|-----------------------|
| Linux    | SIGSTOP / SIGCONT      | /proc/PID/status      |
| Windows  | SuspendThread / Resume | GetProcessMemoryInfo  |
| macOS    | SIGSTOP / SIGCONT      | proc_pidinfo          |

CPU throttling uses a **duty-cycle** approach: during each polling window
the process runs for `(limit%)` of the window and is suspended for the rest.

Memory enforcement is hard: when RSS exceeds the limit the process is killed
immediately with a clear error message.

---

## Installing the binary

### Windows

1. Download `watchdog-windows.exe` from the [Releases](../../releases) page
2. Rename it to `watchdog.exe` and put it somewhere on your PATH, for example:
   ```cmd
   move watchdog-windows.exe C:\Windows\System32\watchdog.exe
   ```
   Or add a folder of your choice to PATH instead:
   ```cmd
   mkdir C:\tools
   move watchdog-windows.exe C:\tools\watchdog.exe
   :: then add C:\tools to your PATH via System Properties > Environment Variables
   ```
3. Open a new CMD or PowerShell and verify:
   ```cmd
   watchdog --help
   ```

---

### Linux

1. Download `watchdog-linux` from the [Releases](../../releases) page
2. Make it executable and move it to your PATH:
   ```bash
   chmod +x watchdog-linux
   sudo mv watchdog-linux /usr/local/bin/watchdog
   ```
3. Verify:
   ```bash
   watchdog --help
   ```

No install needed beyond that — it's a single static-ish binary with no dependencies.

---

### macOS

1. Download `watchdog-macos` from the [Releases](../../releases) page
2. Make it executable and move it to your PATH:
   ```bash
   chmod +x watchdog-macos
   sudo mv watchdog-macos /usr/local/bin/watchdog
   ```
3. macOS will block it the first time because it's not from the App Store.
   Clear the quarantine flag:
   ```bash
   xattr -d com.apple.quarantine /usr/local/bin/watchdog
   ```
4. Verify:
   ```bash
   watchdog --help
   ```

---

## Building

### Option 1 — GitHub Actions (recommended, builds all 3 platforms at once)

This is the easiest path. GitHub's free CI runs native compilers on Linux,
Windows, and macOS in parallel and attaches the binaries to a Release.

**First time setup:**

1. Create a repo on GitHub and push the code:
   ```
   git init
   git add .
   git commit -m "initial commit"
   git remote add origin https://github.com/YOUR_USERNAME/watchdog.git
   git push -u origin main
   ```

2. Push a version tag to trigger the release build:
   ```
   git tag v1.0.0
   git push origin v1.0.0
   ```

3. Go to **github.com/YOUR_USERNAME/watchdog/actions** and watch the build.

4. When it finishes, go to **github.com/YOUR_USERNAME/watchdog/releases** and
   download the binary for your platform:
   - `watchdog-linux`      — Linux x86-64
   - `watchdog-windows.exe` — Windows x86-64
   - `watchdog-macos`      — macOS (Apple Silicon + Intel via Clang)

**To release a new version:**
```
git add .
git commit -m "your changes"
git tag v1.1.0
git push origin main
git push origin v1.1.0
```
That's it. The Actions workflow triggers automatically on any `v*` tag.

---

### Option 2 — Build locally on Linux or macOS

Requirements: `gcc`/`clang` with C++17, `cmake` 3.16+.

```bash
# Install cmake if needed
# Ubuntu/Debian:  sudo apt install cmake
# macOS:          brew install cmake

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/watchdog --help
```

---

### Option 3 — Build locally on Windows (MSYS2)

Requirements: [MSYS2](https://www.msys2.org/) with MinGW-w64.

Open the **MSYS2 MinGW x64** terminal (not "MSYS2 MSYS"):

```bash
# Install cmake and ninja (one time only)
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja

# Build
cd /e/path/to/watchdog
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/watchdog.exe --help
```

---

### Option 4 — Build a Linux binary via Docker (no compiler needed locally)

Requirements: [Docker Desktop](https://www.docker.com/products/docker-desktop/).

```cmd
mkdir output
docker build -t watchdog-build .
docker run --rm -v "%cd%\output":/out watchdog-build cp /src/build/watchdog /out/watchdog
```

The Linux binary ends up in `output\watchdog`.  
To test it immediately in WSL2:
```bash
wsl ./output/watchdog --help
```

---

## Project layout

```
watchdog/
├── .github/workflows/build.yml   # CI: builds + releases all 3 platforms
├── Dockerfile                    # builds a Linux binary via Docker
├── CMakeLists.txt
├── include/
│   ├── platform.hpp              # OS abstraction interface
│   ├── throttler.hpp             # throttle loop config + entry point
│   └── cli.hpp                   # argument parsing
└── src/
    ├── main.cpp
    ├── cli.cpp                   # --cpu / --mem / --poll argument parser
    ├── throttler.cpp             # duty-cycle CPU + memory enforcement loop
    ├── platform_linux.cpp        # Linux: posix_spawnp, /proc, SIGSTOP/SIGCONT
    ├── platform_windows.cpp      # Windows: CreateProcess, SuspendThread, psapi
    └── platform_macos.cpp        # macOS: posix_spawnp, proc_pidinfo, SIGSTOP
```

---

## Platform support

| Platform | Compiler | Min version |
|----------|----------|-------------|
| Linux    | GCC / Clang | GCC 9+, kernel 3.2+ |
| Windows  | MSVC / MinGW | MSVC 2019+, Windows 10+ |
| macOS    | Apple Clang | macOS 10.15+ |

Requires C++17. No external dependencies beyond the OS APIs.
