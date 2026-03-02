cmake_minimum_required(VERSION 3.26)
project(idpdisk LANGUAGES CXX)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # allow GNU extensions (__attribute__ etc.)

# ── Default to Debug ──────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ── Output directory: <repo-root>/bin ─────────────────────────────────────────
# CMAKE_SOURCE_DIR points to src/ when invoked with  cmake -S src -B build
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin")

# ── Dependencies ──────────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CLI11)

# ── Target ────────────────────────────────────────────────────────────────────
add_executable(idpdisk
    main.cpp
    cpm_disk.cpp
)

target_link_libraries(idpdisk PRIVATE CLI11::CLI11)

target_compile_options(idpdisk PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:   -g3 -O0 -DDEBUG>
    $<$<CONFIG:Release>: -O2 -DNDEBUG>
)

# ── Status message ────────────────────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Output dir : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
