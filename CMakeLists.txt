cmake_minimum_required(VERSION 3.16)
project(watchdog CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()

include_directories(include)

set(SOURCES
    src/main.cpp
    src/cli.cpp
    src/throttler.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/platform_linux.cpp)
elseif(APPLE)
    list(APPEND SOURCES src/platform_macos.cpp)
elseif(WIN32)
    list(APPEND SOURCES src/platform_windows.cpp)
endif()

add_executable(watchdog ${SOURCES})

# Windows needs psapi for GetProcessMemoryInfo
if(WIN32)
    target_link_libraries(watchdog psapi)
endif()

# Linux: pthread for std::thread internals; rt for clock_gettime on older glibc
if(UNIX AND NOT APPLE)
    target_link_libraries(watchdog pthread)
endif()

install(TARGETS watchdog DESTINATION bin)
