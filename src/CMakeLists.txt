# ── Target ────────────────────────────────────────────────────────────────────
# Project-wide settings (standard, output dir, dependencies) are in the root
# CMakeLists.txt.  This file only defines the cpmdisk executable target.

add_executable(cpmdisk
    main.cpp
    cpm_disk.cpp
)

target_link_libraries(cpmdisk PRIVATE CLI11::CLI11)

target_compile_options(cpmdisk PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:   -g3 -O0 -DDEBUG>
    $<$<CONFIG:Release>: -O2 -DNDEBUG>
)
