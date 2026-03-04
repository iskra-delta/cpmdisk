# ── Target ────────────────────────────────────────────────────────────────────
# CLI executable only.  CP/M logic lives in shared target cpmdisk_lib.

add_executable(cpmdisk
    main.cpp
)

target_include_directories(cpmdisk PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cpmdisk PRIVATE cpmdisk_lib CLI11::CLI11)
