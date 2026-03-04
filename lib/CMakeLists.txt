# Shared CP/M core library.
add_library(cpmdisk_lib SHARED
    cpm_disk.cpp
)

set_target_properties(cpmdisk_lib PROPERTIES
    OUTPUT_NAME cpmdisk
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(cpmdisk_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
)

target_compile_options(cpmdisk_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Zi /Od /DDEBUG>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2 /DNDEBUG>
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-g3 -O0 -DDEBUG>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2 -DNDEBUG>
)
