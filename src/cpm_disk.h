#pragma once

#include "diskdef.h"
#include "direntry.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <array>
#include <string>
#include <vector>

struct create_opts {
    bool        cpm3{false}; // effective directory convention
    std::string label;         // CP/M 3 disk label (8.3 style)
    bool        datestamp{false}; // CP/M 3 directory datestamp metadata marker
};

// ── cpm_disk ────────────────────────────────────────────────────────────────────
//
// Provides create/open factory methods and user-facing disk commands.
//
// The disk image is a flat binary file; tracks are stored sequentially,
// each track holds sectrk sectors of seclen bytes.  No interleave or skew
// is applied (skew = 0 for both IDP disk types).

class cpm_disk {
public:
    // ── Factory ───────────────────────────────────────────────────────────────

    // Create a new, blank disk image (filled with 0x00; directory with 0xE5).
    static cpm_disk create(const std::filesystem::path& path,
                           const disk_def& def,
                           const create_opts& opts = {});

    // Open an existing disk image.
    // If `hint` is provided it is used directly; otherwise the type is inferred
    // from the file size.  Throws if auto-detection fails and no hint is given.
    static cpm_disk open(const std::filesystem::path& path,
                        std::optional<disk_def>        hint = std::nullopt);

    // ── Commands ──────────────────────────────────────────────────────────────

    // Print disk geometry and free-space statistics.
    void cmd_info();

    // List files.  Pass user >= 0 to restrict to one user area; -1 = all.
    void cmd_list(int user = -1);

    // Copy a host file onto the disk in the given CP/M user area.
    void cmd_add(const std::filesystem::path& host_path, int user);

    // Extract files from disk image to host directory.
    void cmd_extract(const std::vector<std::string>& patterns,
                     const std::filesystem::path& out_dir,
                     int user = -1);

    // Rename a file within the image.
    void cmd_rename(const std::string& src_name,
                    const std::string& dst_name,
                    int user = -1,
                    int to_user = -1);

    // Copy a file within the image.
    void cmd_copy(const std::string& src_name,
                  const std::string& dst_name,
                  int user = -1,
                  int to_user = -1);

    // Read/write reserved boot+system track area.
    void cmd_boot_read(const std::filesystem::path& out_path);
    void cmd_boot_write(const std::filesystem::path& in_path);

    // Delete all directory entries whose "NAME.EXT" matches the wildcard
    // pattern (* and ? supported).  Pass user >= 0 to restrict; -1 = all.
    void cmd_remove(const std::string& pattern, int user = -1);

    // Accessors
    const disk_def& def() const noexcept { return def_; }

private:
    explicit cpm_disk(std::filesystem::path path, disk_def def);

    // ── Low-level I/O ─────────────────────────────────────────────────────────

    std::vector<dir_entry>  read_dir()  const;
    void                   write_dir(const std::vector<dir_entry>& dir);

    std::vector<uint8_t>   read_block(uint32_t block)                      const;
    void                   write_block(uint32_t block, std::span<const uint8_t> data);

    // ── Allocation ────────────────────────────────────────────────────────────

    // Returns the set of block numbers currently referenced by valid entries,
    // plus the directory blocks (which are always allocated).
    std::set<uint32_t> used_blocks(const std::vector<dir_entry>& dir) const;

    // Find and return the lowest-numbered free block, adding it to `used`.
    uint32_t alloc_block(std::set<uint32_t>& used) const;

    void add_cpm_file(const std::array<char,8>& cpm_name,
                      const std::array<char,3>& cpm_ext,
                      std::span<const uint8_t> data,
                      int user);

    // ── State ─────────────────────────────────────────────────────────────────

    std::filesystem::path path_;
    disk_def               def_;
    mutable std::fstream  file_;
};
