#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// ── Disk geometry ──────────────────────────────────────────────────────────────
// All field names follow the cpmtools diskdef convention.

struct DiskDef {
    const char* name;
    uint32_t    seclen;     // physical sector size in bytes
    uint32_t    tracks;     // total number of tracks
    uint32_t    sectrk;     // sectors per track
    uint32_t    blocksize;  // allocation block size in bytes
    uint32_t    maxdir;     // maximum number of directory entries
    uint32_t    skew;       // sector skew factor (0 = no skew)
    uint32_t    boottrk;    // number of reserved boot tracks

    // ── Derived geometry ───────────────────────────────────────────────────────
    constexpr uint32_t total_sectors()   const noexcept { return tracks * sectrk; }
    constexpr uint32_t boot_sectors()    const noexcept { return boottrk * sectrk; }
    constexpr uint32_t data_sectors()    const noexcept { return total_sectors() - boot_sectors(); }
    constexpr uint32_t secs_per_block()  const noexcept { return blocksize / seclen; }
    constexpr uint32_t total_blocks()    const noexcept { return data_sectors() / secs_per_block(); }
    constexpr uint64_t disk_size()       const noexcept { return uint64_t(total_sectors()) * seclen; }

    // Number of data blocks used for the directory.
    constexpr uint32_t dir_blocks() const noexcept {
        return (maxdir * 32u + blocksize - 1u) / blocksize;
    }

    // Block pointers are 2 bytes (little-endian) when total_blocks > 255.
    constexpr bool     use_word_blocks()   const noexcept { return total_blocks() > 255; }
    constexpr uint32_t ptrs_per_extent()   const noexcept { return use_word_blocks() ? 8u : 16u; }

    // How many blocks one logical 16 KB extent actually uses.
    // CP/M always defines one logical extent = 128 records x 128 bytes = 16 384 bytes.
    constexpr uint32_t blocks_per_extent() const noexcept {
        uint32_t bpe = 16384u / blocksize;
        return bpe < ptrs_per_extent() ? bpe : ptrs_per_extent();
    }

    // Byte offset in the disk image for the first byte of a data block.
    constexpr uint64_t block_offset(uint32_t block) const noexcept {
        return uint64_t(boot_sectors() + block * secs_per_block()) * seclen;
    }
};

// ── Known disk definitions ─────────────────────────────────────────────────────

inline constexpr DiskDef DISK_FDD {
    "idpfdd",
    /*seclen*/    256,
    /*tracks*/    146,
    /*sectrk*/    18,
    /*blocksize*/ 2048,
    /*maxdir*/    128,
    /*skew*/      0,
    /*boottrk*/   2
};

inline constexpr DiskDef DISK_HDD {
    "idphdd",
    /*seclen*/    256,
    /*tracks*/    1224,
    /*sectrk*/    32,
    /*blocksize*/ 4096,
    /*maxdir*/    1024,
    /*skew*/      0,
    /*boottrk*/   1
};

// Look up a definition by short name ("fdd"/"hdd") or full name ("idpfdd"/"idphdd").
inline std::optional<DiskDef> find_diskdef(std::string_view name) noexcept {
    if (name == "fdd" || name == "idpfdd") return DISK_FDD;
    if (name == "hdd" || name == "idphdd") return DISK_HDD;
    return std::nullopt;
}

// Detect disk type from image file size.
inline std::optional<DiskDef> diskdef_by_size(uint64_t size) noexcept {
    if (size == DISK_FDD.disk_size()) return DISK_FDD;
    if (size == DISK_HDD.disk_size()) return DISK_HDD;
    return std::nullopt;
}
