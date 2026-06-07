cmake_minimum_required(VERSION 3.16)
project(watchdog_project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

if(WIN32)
    target_link_libraries(watchdog psapi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(watchdog pthread)
endif()

install(TARGETS watchdog DESTINATION bin)
