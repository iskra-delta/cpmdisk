#pragma once

#include "diskdef.h"
#include "direntry.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

// ── CpmDisk ────────────────────────────────────────────────────────────────────
//
// Provides create / open factory methods and the four user-facing commands:
//   info, list, add, remove
//
// The disk image is a flat binary file; tracks are stored sequentially,
// each track holds sectrk sectors of seclen bytes.  No interleave or skew
// is applied (skew = 0 for both IDP disk types).

class CpmDisk {
public:
    // ── Factory ───────────────────────────────────────────────────────────────

    // Create a new, blank disk image (filled with 0x00; directory with 0xE5).
    static CpmDisk create(const std::filesystem::path& path, const DiskDef& def);

    // Open an existing disk image; disk type is inferred from the file size.
    static CpmDisk open(const std::filesystem::path& path);

    // ── Commands ──────────────────────────────────────────────────────────────

    // Print disk geometry and free-space statistics.
    void cmd_info();

    // List files.  Pass user >= 0 to restrict to one user area; -1 = all.
    void cmd_list(int user = -1);

    // Copy a host file onto the disk in the given CP/M user area.
    void cmd_add(const std::filesystem::path& host_path, int user);

    // Delete all directory entries whose "NAME.EXT" matches the wildcard
    // pattern (* and ? supported).  Pass user >= 0 to restrict; -1 = all.
    void cmd_remove(const std::string& pattern, int user = -1);

    // Accessors
    const DiskDef& def() const noexcept { return def_; }

private:
    explicit CpmDisk(std::filesystem::path path, DiskDef def);

    // ── Low-level I/O ─────────────────────────────────────────────────────────

    std::vector<DirEntry>  read_dir()  const;
    void                   write_dir(const std::vector<DirEntry>& dir);

    std::vector<uint8_t>   read_block(uint32_t block)                      const;
    void                   write_block(uint32_t block, std::span<const uint8_t> data);

    // ── Allocation ────────────────────────────────────────────────────────────

    // Returns the set of block numbers currently referenced by valid entries,
    // plus the directory blocks (which are always allocated).
    std::set<uint32_t> used_blocks(const std::vector<DirEntry>& dir) const;

    // Find and return the lowest-numbered free block, adding it to `used`.
    uint32_t alloc_block(std::set<uint32_t>& used) const;

    // ── State ─────────────────────────────────────────────────────────────────

    std::filesystem::path path_;
    DiskDef               def_;
    mutable std::fstream  file_;
};
