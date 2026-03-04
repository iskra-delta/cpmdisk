# ── Target ────────────────────────────────────────────────────────────────────
# Project-wide settings (standard, output dir, dependencies) are in the root
# CMakeLists.txt.  This file only defines the cpmdisk executable target.

add_executable(cpmdisk
    main.cpp
    cpm_disk.cpp
)

target_link_libraries(cpmdisk PRIVATE CLI11::CLI11)

target_compile_options(cpmdisk PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Zi /Od /DDEBUG>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2 /DNDEBUG>
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-g3 -O0 -DDEBUG>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2 -DNDEBUG>
)
